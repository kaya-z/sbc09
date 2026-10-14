#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""fitslots.py - set the code and data slot sizes of memmap.mk to what the modules need now.

    fitslots.py [BUILD_DIR]      (after a successful build; then run make again)

The slot sizes are the size of the module rounded up to 16 bytes plus 16 bytes of room; the data
slot is bss + data + the stack room that the Makefile asks for (tty: B4, disk: 82, fs: 140, pm: C8)."""
import re, sys

b = sys.argv[1] if len(sys.argv) > 1 else 'build'
room = {'kernel': 0, 'tty': 0xB4, 'disk': 0x82, 'fs': 0x140, 'pm': 0xC8}
key = {'kernel': 'K', 'tty': 'TTY', 'disk': 'DISK', 'fs': 'FS', 'pm': 'PM'}
new = {}
for m, k in key.items():
    code = data = 0
    base = None
    for l in open('%s/%s.map' % (b, m)):
        x = re.match(r'Section: (\w+) \(.*\) load at ([0-9A-F]+), length ([0-9A-F]+)', l)
        if not x:
            continue
        n, ln = x.group(1), int(x.group(3), 16)
        if n in ('start', 'code', 'rodata', 'initgl_start', 'initgl', 'initgl_end'):
            code += ln
        elif n in ('rwdata', 'bss'):
            data += ln
    up = lambda v: (v + 15) // 16 * 16
    new[k + '_CSZ'] = up(code) + 16
    new[k + '_DSZ'] = up(data + room[m]) + (0 if m == 'kernel' else 0)
src = open('memmap.mk', encoding='utf-8').read()
for name, v in new.items():
    src = re.sub(r'^%s\s*=\s*[0-9A-Fa-f]+' % name, '%-10s = %04X' % (name, v), src, flags=re.M)
open('memmap.mk', 'w', encoding='utf-8').write(src)
print(', '.join('%s=%04X' % kv for kv in sorted(new.items())))
