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

import sys, os
import pefile

shimbase, stubdll, outdir, target_noext, primary_path, fallback = sys.argv[1:7]
donors = sys.argv[7:]
assert len(shimbase) == len(target_noext), f"shim base must be {len(target_noext)} chars to match {target_noext}"
assert donors, "need at least one donor"
target_dll = target_noext.lower() + ".dll"

def exports(path):
    names, ords = set(), set()
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    for s in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if s.address or s.forwarder:
            ords.add(s.ordinal)
            if s.name:
                names.add(s.name.decode('ascii', 'replace'))
    pe.close()
    return names, ords

prim_names, prim_ords = exports(primary_path)
prim_mod = os.path.splitext(os.path.basename(primary_path))[0]
fb_names, fb_ords, fb_mod = set(), set(), None
if fallback and fallback != '-':
    fb_names, fb_ords = exports(fallback)
    fb_mod = os.path.splitext(os.path.basename(fallback))[0]

shim_dll = shimbase.lower() + ".dll"
named, ords = set(), set()
for d in donors:
    pe = pefile.PE(d, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
    for e in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) or []:
        if e.dll.decode('ascii', 'replace').lower() not in (target_dll, shim_dll):
            continue
        for imp in e.imports:
            if imp.name is None:
                ords.add(imp.ordinal)
            else:
                named.add(imp.name.decode('ascii', 'replace'))
    pe.close()

d = [f"LIBRARY {shimbase}", "EXPORTS"]
stub_names, stub_ords = [], []
for n in sorted(named):
    if n in prim_names:
        d.append(f"    {n}={prim_mod}.{n}")
    elif n in fb_names:
        d.append(f"    {n}={fb_mod}.{n}")
    else:
        d.append(f"    {n}={stubdll}.{n}")
        stub_names.append(n)
for o in sorted(ords):
    if o in prim_ords:
        d.append(f"    keelfwd_{o}={prim_mod}.#{o} @{o} NONAME")
    elif o in fb_ords:
        d.append(f"    keelfwd_{o}={fb_mod}.#{o} @{o} NONAME")
    else:
        d.append(f"    keelstub_{o}={stubdll}.KeelStub_{o} @{o} NONAME")
        stub_ords.append(o)
open(os.path.join(outdir, f"{shimbase}.def"), "w").write("\n".join(d) + "\n")

c = ["/* Keel dep-seam stubs, no-op replacements for symbols absent from the host primary and fallback */"]
sd = [f"LIBRARY {stubdll}", "EXPORTS"]
for n in stub_names:
    c.append(f"long __stdcall {n}(void) {{ return 0; }}")
    sd.append(f"    {n}")
for o in stub_ords:
    c.append(f"long __stdcall KeelStub_{o}(void) {{ return 0; }}")
    sd.append(f"    KeelStub_{o}")
open(os.path.join(outdir, f"{stubdll}.c"), "w").write("\n".join(c) + "\n")
open(os.path.join(outdir, f"{stubdll}.def"), "w").write("\n".join(sd) + "\n")

print(f"target={target_dll} donors={[os.path.basename(x) for x in donors]} "
      f"named={len(named)} (fwd-primary={len([n for n in named if n in prim_names])}, "
      f"fwd-fallback={len([n for n in named if n not in prim_names and n in fb_names])}, "
      f"stub={stub_names}) ords={sorted(ords)} stub-ords={stub_ords}")
