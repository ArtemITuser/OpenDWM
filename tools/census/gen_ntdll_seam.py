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

import os
import sys
import pefile

host_ntdll, shimbase, stubdll, outdir = sys.argv[1:5]
donors = sys.argv[5:]
assert len(shimbase) == len("ntdll"), "shim base name must be 5 chars"
assert donors, "need at least one donor DLL"

host_names, host_ords = set(), set()
hpe = pefile.PE(host_ntdll, fast_load=True)
hpe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
for e in hpe.DIRECTORY_ENTRY_EXPORT.symbols:
    if e.address:
        host_ords.add(e.ordinal)
        if e.name:
            host_names.add(e.name.decode("ascii", "replace"))
hpe.close()

named, ords = set(), set()
for d in donors:
    pe = pefile.PE(d, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
    for attr in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for entry in getattr(pe, attr, []) or []:
            if entry.dll.decode("ascii", "replace").lower() != "ntdll.dll":
                continue
            for imp in entry.imports:
                if imp.name is None:
                    ords.add(imp.ordinal)
                else:
                    named.add(imp.name.decode("ascii", "replace"))
    pe.close()

named = sorted(named)
ords = sorted(ords)
fwd_named = [n for n in named if n in host_names]
stub_named = [n for n in named if n not in host_names]
fwd_ord = [o for o in ords if o in host_ords]
stub_ord = [o for o in ords if o not in host_ords]

d = [f"LIBRARY {shimbase}", "EXPORTS"]
for n in fwd_named:
    d.append(f"    {n}=ntdll.{n}")
for n in stub_named:
    d.append(f"    {n}={stubdll}.{n}")
for o in fwd_ord:
    d.append(f"    keelntfwd_{o}=ntdll.#{o} @{o} NONAME")
for o in stub_ord:
    d.append(f"    keelnt_{o}={stubdll}.KeelNt_{o} @{o} NONAME")
open(os.path.join(outdir, f"{shimbase}.def"), "w").write("\n".join(d) + "\n")

c = ["/* Keel ntdll-seam stubs, no-op replacements for Win7 ntdll ordinals absent from Win10 ntdll */"]
sd = [f"LIBRARY {stubdll}", "EXPORTS"]
for n in stub_named:
    c.append(f"long __stdcall {n}(void) {{ return 0; }}")
    sd.append(f"    {n}")
for o in stub_ord:
    c.append(f"long __stdcall KeelNt_{o}(void) {{ return 0; }}")
    sd.append(f"    KeelNt_{o}")
open(os.path.join(outdir, f"{stubdll}.c"), "w").write("\n".join(c) + "\n")
open(os.path.join(outdir, f"{stubdll}.def"), "w").write("\n".join(sd) + "\n")

print(f"donors={[os.path.basename(x) for x in donors]} forward-named={len(fwd_named)} "
      f"forward-ord={fwd_ord} stub-named={stub_named} stub-ord={stub_ord}")
