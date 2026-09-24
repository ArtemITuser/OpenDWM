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

import sys
from pathlib import Path

import pefile

CANDIDATES = ["kernelbase.dll", "advapi32.dll", "kernel32.dll", "sechost.dll", "ntdll.dll",
              "rpcrt4.dll", "user32.dll", "gdi32.dll", "combase.dll", "ole32.dll", "shcore.dll"]

def exports(path):
    names = []
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]])
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if e.name:
                names.append(e.name.decode("ascii", "replace"))
    pe.close()
    return names

def host_index(host_root):
    idx = {}
    sys32 = Path(host_root) / "System32"
    for dll in CANDIDATES:
        p = sys32 / dll
        if not p.exists():
            continue
        mod = dll[:-4]
        for name in exports(p):
            idx.setdefault(name, mod)
    return idx

def main():
    if len(sys.argv) < 4:
        print("gen_apiset_def.py <win7-stub.dll> <host-root> <out.def>", file=sys.stderr)
        sys.exit(2)
    stub, host_root, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    idx = host_index(host_root)
    lines = ["; generated forwarder for %s" % stub.name, "EXPORTS"]
    missing = []
    for name in exports(stub):
        tgt = idx.get(name)
        if tgt:
            lines.append("    %s=%s.%s" % (name, tgt, name))
        else:
            missing.append(name)
    out.write_text("\n".join(lines) + "\n", encoding="ascii")
    print("wrote %s with %d forwarders, %d unresolved" % (out, len(lines) - 2, len(missing)))
    if missing:
        print("  unresolved " + ", ".join(missing), file=sys.stderr)

if __name__ == "__main__":
    main()
