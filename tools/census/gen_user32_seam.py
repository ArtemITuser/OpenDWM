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

host_user32, shimbase, outdir = sys.argv[1:4]
donors = sys.argv[4:]
assert len(shimbase) == len("user32"), "shim base name must be 6 chars"
assert donors, "need at least one donor DLL"

host_names, host_ords = set(), set()
hpe = pefile.PE(host_user32, fast_load=True)
hpe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
for e in hpe.DIRECTORY_ENTRY_EXPORT.symbols:
    if e.address or e.forwarder:
        host_ords.add(e.ordinal)
        if e.name:
            host_names.add(e.name.decode("ascii", "replace"))
hpe.close()

named, ords = set(), set()
for donor in donors:
    pe = pefile.PE(donor, fast_load=True)
    pe.parse_data_directories(directories=[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])
    want = {"user32.dll", shimbase.lower() + ".dll"}
    for attr in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT"):
        for entry in getattr(pe, attr, []) or []:
            if entry.dll.decode("ascii", "replace").lower() not in want:
                continue
            for imp in entry.imports:
                if imp.name is None:
                    ords.add(imp.ordinal)
                else:
                    named.add(imp.name.decode())
    pe.close()
named = sorted(named); ords = sorted(ords)

_here = os.path.dirname(os.path.abspath(__file__))
_raw_user32 = os.path.join(_here, '..', '..', 'donor', 'raw', 'Windows', 'System32', 'user32.dll')
_w7csv = os.path.join(_here, 'w7-user32-funcs.csv')
w7_ord_name = {}
if os.path.exists(_raw_user32) and os.path.exists(_w7csv):
    rva2name = {}
    for line in open(_w7csv):
        p = line.strip().split(',', 2)
        if len(p) == 3 and p[0] != 'rva':
            try: rva2name[int(p[0], 16)] = p[2]
            except ValueError: pass
    _pe = pefile.PE(_raw_user32, fast_load=True)
    _pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
    for s in _pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if s.name: w7_ord_name[s.ordinal] = s.name.decode('ascii', 'replace')
        elif not s.forwarder and s.address in rva2name: w7_ord_name[s.ordinal] = rva2name[s.address]
    _pe.close()
ord_named_fwd = {}
ord_stub_named = {}
for o in ords:
    n = w7_ord_name.get(o)
    if n and n in host_names: ord_named_fwd[o] = n
    elif n: ord_stub_named[o] = n

fwd_named = [n for n in named if n in host_names]
stub_named = [n for n in named if n not in host_names]
fwd_ord = [o for o in ords if o not in ord_named_fwd and o not in ord_stub_named and o in host_ords]
stub_ord = [o for o in ords if o not in ord_named_fwd and o not in ord_stub_named and o not in host_ords]
for o in fwd_ord:
    print(f"!WARNING! ordinal #{o} forwarded by NUMBER since its Win7 meaning is unknown, add w7-user32-funcs.csv")

d = [f"LIBRARY {shimbase}", "EXPORTS"]
for n in fwd_named:
    d.append(f"    {n}=user32.{n}")
for n in stub_named:
    d.append(f"    {n}=keelstub.{n}")
for o, n in sorted(ord_named_fwd.items()):
    d.append(f"    keelfwd_{o}=user32.{n} @{o} NONAME")
for o in fwd_ord:
    d.append(f"    keelfwd_{o}=user32.#{o} @{o} NONAME")
for o in sorted(ord_stub_named):
    d.append(f"    keelstub_{o}=keelstub.keelstub_{o} @{o} NONAME")
for o in stub_ord:
    d.append(f"    keelstub_{o}=keelstub.keelstub_{o} @{o} NONAME")
open(os.path.join(outdir, f"{shimbase}.def"), "w").write("\n".join(d) + "\n")

c = ['#include <windows.h>', '']
sd = ["LIBRARY keelstub", "EXPORTS"]
for n in stub_named:
    c.append(f"long __stdcall {n}(void) {{ return (long)0x80004001; }}")
    sd.append(f"    {n}")
for o, n in sorted(ord_stub_named.items()):
    c.append(f"long __stdcall keelstub_{o}(void) {{ return 0; }}   /* Win7 user32!{n} (#{o}) */")
    sd.append(f"    keelstub_{o}")
for o in stub_ord:
    c.append(f"long __stdcall keelstub_{o}(void) {{ return (long)0x80004001; }}")
    sd.append(f"    keelstub_{o}")
open(os.path.join(outdir, "keelstub.c"), "w").write("\n".join(c) + "\n")
open(os.path.join(outdir, "keelstub.def"), "w").write("\n".join(sd) + "\n")

print(f"donors={[os.path.basename(x) for x in donors]} forward-named={len(fwd_named)} "
      f"forward-ord={len(fwd_ord)} stub-named={stub_named} stub-ord={stub_ord}")
