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
"""copies every resource of a MUI file into its language-neutral binary and drops the MUI config, args <ln-binary> <mui-file>"""

import ctypes, ctypes.wintypes as wt, sys
import pefile

k32 = ctypes.windll.kernel32
k32.BeginUpdateResourceW.restype = wt.HANDLE
k32.BeginUpdateResourceW.argtypes = [wt.LPCWSTR, wt.BOOL]
k32.UpdateResourceW.restype = wt.BOOL
k32.UpdateResourceW.argtypes = [wt.HANDLE, wt.LPCWSTR, wt.LPCWSTR, wt.WORD, wt.LPVOID, wt.DWORD]
k32.EndUpdateResourceW.restype = wt.BOOL
k32.EndUpdateResourceW.argtypes = [wt.HANDLE, wt.BOOL]


def entries(path):
    pe = pefile.PE(path)
    out = []
    for t in (pe.DIRECTORY_ENTRY_RESOURCE.entries if hasattr(pe, 'DIRECTORY_ENTRY_RESOURCE') else []):
        tkey = str(t.name) if t.name else t.id
        for n in t.directory.entries:
            nkey = str(n.name) if n.name else n.id
            for l in n.directory.entries:
                d = l.data.struct
                out.append((tkey, nkey, l.id, pe.get_data(d.OffsetToData, d.Size)))
    pe.close()
    return out


def res_id(key):
    return ctypes.cast(key, wt.LPCWSTR) if isinstance(key, int) else key


ln, mui = sys.argv[1], sys.argv[2]
have = {(t, n, l) for t, n, l, _ in entries(ln)}
add = [e for e in entries(mui) if e[0] != 'MUI']
drop = [(t, n, l) for t, n, l in have if t == 'MUI']
if not drop and all((t, n, l) in have for t, n, l, _ in add):
    print(f'{ln} already carries its MUI resources'); sys.exit(0)

def update(items, label):
    h = k32.BeginUpdateResourceW(ln, False)
    if not h: raise SystemExit(f'BeginUpdateResource failed on {ln}: {ctypes.GetLastError()}')
    for t, n, l, data in items:
        buf = ctypes.create_string_buffer(data, len(data)) if data else None
        if not k32.UpdateResourceW(h, res_id(t), res_id(n), l, buf, len(data) if data else 0):
            raise SystemExit(f'{label} {t}/{n}/{l} failed on {ln}: {ctypes.GetLastError()}')
    if not k32.EndUpdateResourceW(h, False): raise SystemExit(f'EndUpdateResource failed on {ln}: {ctypes.GetLastError()}')


# UpdateResource refuses to add to a file that still has an LN MUI config, so the config goes first in its own pass
if drop: update([(t, n, l, None) for t, n, l in drop], 'removing')
update(add, 'adding')
print(f'{ln} embedded {len(add)} resources from {mui}, dropped {len(drop)} MUI config')
