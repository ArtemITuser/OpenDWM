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

import os, sys, codecs
from Registry import Registry

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = {a.split('=')[0]: (a.split('=')[1] if '=' in a else True) for a in sys.argv[1:] if a.startswith('--')}
win7_path, guest_path, reg_root, out_dir = args[:4]
only_prefix = opts.get('--prefix')
max_bytes = int(opts.get('--max-mb', 8)) * 1024 * 1024
os.makedirs(out_dir, exist_ok=True)

# python-registry returns "(default)" for the unnamed value AND for a literal "(default)" value, only has_name() tells them apart
def vname(v):
    return v.name() if v._vkrecord.has_name() else ''

def walk(hive):
    """{lowercased key path: set(lowercased value names)} for every key in the hive."""
    out = {}
    stack = [(hive.root(), '')]
    while stack:
        key, path = stack.pop()
        try: vals = {vname(v).lower() for v in key.values()}
        except Exception: vals = set()
        out[path.lower()] = vals
        try: subs = key.subkeys()
        except Exception: subs = []
        for s in subs:
            stack.append((s, f'{path}\\{s.name()}' if path else s.name()))
    return out

def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def hexbytes(b, kind):
    body = ','.join(f'{x:02x}' for x in b)
    return f'hex({kind}):{body}' if kind else f'hex:{body}'

def fmt(v):
    """formats one value as a .reg right-hand side, or None if unsupported"""
    t, d = v.value_type(), None
    try: d = v.value()
    except Exception: return None
    if t == Registry.RegSZ:
        return '"%s"' % esc(d if isinstance(d, str) else '')
    if t == Registry.RegExpandSZ:
        return hexbytes((d + '\0').encode('utf-16le'), 2)
    if t == Registry.RegDWord:
        return 'dword:%08x' % (int(d) & 0xFFFFFFFF)
    if t == Registry.RegQWord:
        return hexbytes(int(d).to_bytes(8, 'little'), 'b')
    if t == Registry.RegBin:
        return hexbytes(d if isinstance(d, bytes) else bytes(d), 0) if False else 'hex:' + ','.join(f'{x:02x}' for x in (d or b''))
    if t == Registry.RegMultiSZ:
        blob = ''.join(s + '\0' for s in (d or [])) + '\0'
        return hexbytes(blob.encode('utf-16le'), 7)
    if t == Registry.RegNone:
        return hexbytes(d if isinstance(d, bytes) else b'', 0)
    return None

def wrap(line):
    """reg.exe accepts long lines but hex blobs are wrapped the way regedit does for readability"""
    if len(line) <= 4000 or 'hex' not in line: return line
    head, _, body = line.partition(':')
    parts = body.split(',')
    out, cur = [], head + ':'
    for p in parts:
        if len(cur) + len(p) + 1 > 76:
            out.append(cur + '\\'); cur = '  '
        cur += p + ','
    out.append(cur.rstrip(','))
    return '\n'.join(out)

print('walking guest hive...', flush=True)
guest = walk(Registry.Registry(guest_path))
print(f'  guest keys {len(guest)}', flush=True)
print('walking win7 hive...', flush=True)
w7hive = Registry.Registry(win7_path)

files, idx, buf, size = [], 0, [], 0
stat = {'new_keys': 0, 'new_values': 0, 'skipped_values': 0, 'unsupported': 0}

tsv = codecs.open(os.path.join(out_dir, 'merge.tsv'), 'w', 'utf-8')
import base64
def raw_of(v):
    t = v.value_type()
    try: d = v.value()
    except Exception: return None
    if t in (Registry.RegSZ, Registry.RegExpandSZ):
        return ((d or '') + '\0').encode('utf-16le')
    if t == Registry.RegDWord:   return (int(d) & 0xFFFFFFFF).to_bytes(4, 'little')
    if t == Registry.RegQWord:   return (int(d) & (2**64-1)).to_bytes(8, 'little')
    if t == Registry.RegMultiSZ: return (''.join(s + '\0' for s in (d or [])) + '\0').encode('utf-16le')
    if t in (Registry.RegBin, Registry.RegNone):
        return d if isinstance(d, bytes) else bytes(d or b'')
    return None

def flush():
    global buf, size, idx
    if not buf: return
    idx += 1
    p = os.path.join(out_dir, f'merge-{idx:03d}.reg')
    with codecs.open(p, 'w', 'utf-16-le') as f:
        f.write('﻿Windows Registry Editor Version 5.00\r\n\r\n')
        f.write('\r\n'.join(buf) + '\r\n')
    files.append(p); buf, size = [], 0

stack = [(w7hive.root(), '')]
while stack:
    key, path = stack.pop()
    try: subs = key.subkeys()
    except Exception: subs = []
    for s in subs:
        stack.append((s, f'{path}\\{s.name()}' if path else s.name()))
    if only_prefix and path and not (path.lower() == only_prefix.lower() or path.lower().startswith(only_prefix.lower() + '\\')):
        if not only_prefix.lower().startswith(path.lower()):
            continue
        continue
    gvals = guest.get(path.lower())
    key_is_new = gvals is None
    try: vals = key.values()
    except Exception: vals = []
    emit = []
    for v in vals:
        name = vname(v)
        if not key_is_new and name.lower() in gvals:
            stat['skipped_values'] += 1
            continue
        rhs = fmt(v)
        if rhs is None:
            stat['unsupported'] += 1
            continue
        emit.append(wrap(('@=' if name == '' else '"%s"=' % esc(name)) + rhs))
        stat['new_values'] += 1
    if key_is_new: stat['new_keys'] += 1
    if key_is_new or emit:
        full = reg_root + ('\\' + path if path else '')
        block = '[' + full + ']\r\n' + '\r\n'.join(emit) + '\r\n'
        buf.append(block); size += len(block)
        if size >= max_bytes: flush()
    if key_is_new: tsv.write('K\t%s\n' % path)
    for v in vals:
        name = vname(v)
        if not key_is_new and name.lower() in gvals: continue
        raw = raw_of(v)
        if raw is None: continue
        tsv.write('V\t%s\t%s\t%d\t%s\n' % (path, name, v.value_type(), base64.b64encode(raw).decode()))
flush()
tsv.close()
print(f'new keys={stat["new_keys"]} new values={stat["new_values"]} '
      f'skipped(existing)={stat["skipped_values"]} unsupported={stat["unsupported"]}')
print(f'wrote {len(files)} file(s) to {out_dir}')
