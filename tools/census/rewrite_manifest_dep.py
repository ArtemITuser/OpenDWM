#!/usr/bin/env python3
# KeelShim - Windows 7 user-mode UI translation for the Windows 10 kernel
# Copyright (C) 2026 Kevin Dalli <projectkeel@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import ctypes, ctypes.wintypes as wt, re, sys
import pefile

PAT = re.compile(rb'<assemblyIdentity\b[^>]*?name="Microsoft\.Windows\.Common-Controls"[^>]*?(?:/>|>\s*</assemblyIdentity>)', re.S)
NEW = b'<assemblyIdentity type="win32" name="Keel.Common-Controls" version="6.0.7601.23403" processorArchitecture="amd64"/>'
RT_MANIFEST = 24

k32 = ctypes.windll.kernel32
k32.BeginUpdateResourceW.restype = wt.HANDLE
k32.BeginUpdateResourceW.argtypes = [wt.LPCWSTR, wt.BOOL]
k32.UpdateResourceW.restype = wt.BOOL
k32.UpdateResourceW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, wt.WORD, wt.LPVOID, wt.DWORD]
k32.EndUpdateResourceW.restype = wt.BOOL
k32.EndUpdateResourceW.argtypes = [wt.HANDLE, wt.BOOL]

# mt.exe cannot read some of these resources (networkexplorer #123 exits 31) so write through UpdateResourceW
for path in sys.argv[1:]:
    pe = pefile.PE(path, fast_load=True)
    # Keel.Common-Controls is an amd64 assembly, an x86 binary bound to it would fail to load
    if pe.FILE_HEADER.Machine != 0x8664:
        pe.close()
        print(f'{path} is not amd64 so it keeps its own Common-Controls dependency')
        continue
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    todo = []
    for e in (pe.DIRECTORY_ENTRY_RESOURCE.entries if hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE') else []):
        if e.id != RT_MANIFEST: continue
        for e2 in e.directory.entries:
            if e2.id is None: continue
            for e3 in e2.directory.entries:
                d = e3.data.struct
                data = pe.get_data(d.OffsetToData, d.Size)
                if PAT.search(data):
                    todo.append((e2.id, e3.id, PAT.sub(NEW, data)))
    pe.close()
    if not todo:
        print(f'{path} has no manifest carrying the Win10 Common-Controls dependency'); continue
    h = k32.BeginUpdateResourceW(path, False)
    if not h: raise SystemExit(f'BeginUpdateResource failed on {path}: {ctypes.GetLastError()}')
    for rid, lang, data in todo:
        buf = ctypes.create_string_buffer(data, len(data))
        if not k32.UpdateResourceW(h, ctypes.cast(RT_MANIFEST, wt.LPCWSTR), ctypes.cast(rid, wt.LPCWSTR), lang, buf, len(data)):
            raise SystemExit(f'UpdateResource failed on {path} #{rid} lang {lang}: {ctypes.GetLastError()}')
    if not k32.EndUpdateResourceW(h, False): raise SystemExit(f'EndUpdateResource failed on {path}: {ctypes.GetLastError()}')
    print(f'{path} rewrote ' + ', '.join(f'#{rid} (lang {lang})' for rid, lang, _ in todo))
