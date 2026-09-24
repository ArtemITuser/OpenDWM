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

import sys, os, glob, re
from Registry import Registry

hive_path, cut3_dir, cur_manifest, out_manifest = sys.argv[1:5]

EXCLUDE = {'comctl32.dll'}
cut3_dlls = sorted({os.path.basename(p) for p in glob.glob(os.path.join(cut3_dir, '*.dll'))
                    if os.path.basename(p).lower() not in EXCLUDE})
cut3_lower = {d.lower(): d for d in cut3_dlls}

reg = Registry.Registry(hive_path)

def default_val(key):
    try:
        return key.value("(default)").value()
    except Exception:
        return None

def sub_named(key, name):
    for s in key.subkeys():
        if s.name().lower() == name:
            return s
    return None

classes = {}
try:
    clsid_root = reg.open("Classes\\CLSID")
except Exception as ex:
    sys.exit(f"cannot open Classes\\CLSID: {ex}")

VALID_TM = {'apartment', 'both', 'free', 'neutral'}
for sub in clsid_root.subkeys():
    clsid = sub.name()
    if not clsid.startswith('{'):
        continue
    inproc = sub_named(sub, 'inprocserver32')
    if inproc is None:
        continue
    dllpath = default_val(inproc)
    if not dllpath:
        continue
    try:
        tm = inproc.value('ThreadingModel').value()
    except Exception:
        tm = None
    base = os.path.basename(str(dllpath).strip().strip('"').replace('%SystemRoot%', '').lstrip('\\')).lower()
    if base in cut3_lower:
        tmv = (tm or 'Apartment')
        if tmv.lower() not in VALID_TM:
            tmv = 'Apartment'
        classes.setdefault(cut3_lower[base], []).append((clsid.lower(), tmv))

lines = ["  <!-- Keel-injected file redirections + reg-free COM -->"]
n_classes = 0
seen_clsid = set()
for dll in cut3_dlls:
    cl = classes.get(dll, [])
    uniq = []
    for clsid, tm in cl:
        if clsid in seen_clsid:
            continue
        seen_clsid.add(clsid)
        uniq.append((clsid, tm))
    if uniq:
        lines.append(f'  <file name="{dll}">')
        for clsid, tm in sorted(uniq):
            lines.append(f'    <comClass clsid="{clsid}" threadingModel="{tm}"/>')
            n_classes += 1
        lines.append('  </file>')
    else:
        lines.append(f'  <file name="{dll}"/>')
block = "\n".join(lines) + "\n"

xml = open(cur_manifest, encoding='utf-8', errors='replace').read()
xml = re.sub(r'\s*<!-- Keel-injected file redirections.*?(?=</assembly>)', '', xml, flags=re.S)
merged = re.sub(r'</assembly>\s*$', block + '</assembly>\n', xml)

with open(out_manifest, 'w', encoding='utf-8') as f:
    f.write(merged)

print(f"cut3 files={len(cut3_dlls)} comClass entries={n_classes} "
      f"(DLLs with classes {sorted(d for d in classes if classes[d])})")
