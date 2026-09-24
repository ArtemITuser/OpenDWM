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
import pefile

path, old, new = sys.argv[1], sys.argv[2], sys.argv[3]
assert len(old) == len(new), "names must be equal length for in-place patch"
old_dll = (old + ".dll").lower()

pe = pefile.PE(path)
pe.parse_data_directories(directories=[
    pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
    pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT"]])

patched = 0
targets = []
for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []) or []:
    if entry.dll.decode("ascii", "replace").lower() == old_dll:
        targets.append(entry.struct.Name)
for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []) or []:
    if entry.dll.decode("ascii", "replace").lower() == old_dll:
        targets.append(entry.struct.pName if hasattr(entry.struct, "pName") else entry.struct.Name)

data = bytearray(pe.__data__)
for name_rva in set(targets):
    off = pe.get_offset_from_rva(name_rva)
    orig = data[off:off + len(old_dll)].decode("ascii", "replace")
    newbytes = (new + ".dll").encode("ascii")
    data[off:off + len(newbytes)] = newbytes
    patched += 1

if patched:
    pe.__data__ = bytes(data)
    pe.write(path)
    print(f"patched {patched} import name(s) {old_dll} -> {new}.dll in {path}")
else:
    print(f"no {old_dll} import found in {path}")
pe.close()
