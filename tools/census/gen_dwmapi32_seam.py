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

w7, w10, host_user32, outdir = sys.argv[1:5]
os.makedirs(outdir, exist_ok=True)

STUB_ARGBYTES = {"SfmDxBindSwapChain": 0xC, "SfmDxReleaseSwapChain": 0x8}

def exports(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    out = {}
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if not (e.address or e.forwarder):
            continue
        out[e.ordinal] = e.name.decode("ascii") if e.name else None
    pe.close()
    return out

def imports_from(path, dll):
    """(named imports, ordinal-only imports) of `path` from `dll`."""
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    names, ords = [], []
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        if d.dll.decode("ascii", "replace").lower() == dll:
            names += [i.name.decode("ascii") for i in d.imports if i.name]
            ords += [i.ordinal for i in d.imports if not i.name]
    pe.close()
    return names, ords

e7, e10 = exports(w7), exports(w10)
n7 = {n for n in e7.values() if n}
host_u32 = exports(host_user32)
user32_have = {n for n in host_u32.values() if n}

lines = ["LIBRARY dwmapi", "EXPORTS"]
to7 = to10 = 0
for ordn in sorted(e10):
    name = e10[ordn]
    if name and name in n7:
        lines.append(f"    {name}=dwmapi7.{name} @{ordn}")
        to7 += 1
    elif name:
        lines.append(f"    {name}=dwmapi10.{name} @{ordn}")
        to10 += 1
    else:
        lines.append(f"    ord{ordn}=dwmapi10.#{ordn} @{ordn} NONAME")
        to10 += 1
missing7 = sorted(n for n in n7 if n not in set(e10.values()))
if missing7:
    base = max(e10) + 1
    for i, name in enumerate(missing7):
        lines.append(f"    {name}=dwmapi7.{name} @{base + i}")
with open(os.path.join(outdir, "dwmapi.def"), "w") as f:
    f.write("\n".join(lines) + "\n")

u_names, u_ords = imports_from(w7, "user32.dll")
u = sorted(set(u_names))
stubbed = [n for n in u if n not in user32_have]
unexpected = [n for n in stubbed if n not in STUB_ARGBYTES]
if unexpected:
    sys.exit("user32 imports missing on Win10 with no measured stub arity: %s" % unexpected)
k = ["LIBRARY keel32", "EXPORTS"]
for n in u:
    k.append(f"    {n}=keelstub.{n}" if n in stubbed else f"    {n}=user32.{n}")
for o in sorted(set(u_ords)):
    if o not in host_u32:
        sys.exit("user32 ordinal #%d imported by Win7 dwmapi is absent from the host user32" % o)
    k.append(f"    keelfwd_{o}=user32.#{o} @{o} NONAME")
with open(os.path.join(outdir, "keel32.def"), "w") as f:
    f.write("\n".join(k) + "\n")

with open(os.path.join(outdir, "keelstub.def"), "w") as f:
    f.write("LIBRARY keelstub\nEXPORTS\n" + "".join(f"    {n}\n" for n in stubbed))
c = ["// Win7-only user32 exports that Win7 x86 dwmapi imports and Win10 lacks, E_NOTIMPL stubs sized from the real ret N since x86 callee pops",
     "#include <windows.h>"]
for n in stubbed:
    nargs = STUB_ARGBYTES[n] // 4
    params = ", ".join(f"void* a{i}" for i in range(nargs)) or "void"
    unused = "".join(f" (void)a{i};" for i in range(nargs))
    c.append(f"long __stdcall {n}({params}) {{{unused} return (long)0x80004001; }}")
with open(os.path.join(outdir, "keelstub.c"), "w") as f:
    f.write("\n".join(c) + "\n")

print(f"dwmapi.def {len(e10)} Win10 exports -> {to7} to dwmapi7, {to10} to dwmapi10"
      + (f", +{len(missing7)} Win7-only appended" if missing7 else ""))
print(f"keel32.def {len(u)} user32 imports, {len(stubbed)} stubbed in keelstub {stubbed}")
