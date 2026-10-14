#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""chkmap.py - check that a linked module fits its slots.

    chkmap.py NAME MAPFILE CODE_BASE CODE_SIZE [DATA_BASE DATA_SIZE MIN_STACK]

Reads the section list of an lwlink map ("Section: name (file) load at XXXX, length YYYY")."""
import re, sys

name, mp = sys.argv[1], sys.argv[2]
cb, cs = int(sys.argv[3], 16), int(sys.argv[4], 16)
secs = []
for line in open(mp):
    m = re.match(r'Section: (\w+) \(.*\) load at ([0-9A-F]+), length ([0-9A-F]+)', line)
    if m:
        secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
code_end = max(a + l for n, a, l in secs if n in ('start', 'code', 'rodata', 'initgl_start', 'initgl', 'initgl_end'))
ok = code_end <= cb + cs
msg = '%-7s code %04X-%04X (%d of %d bytes)' % (name, cb, code_end, code_end - cb, cs)
if len(sys.argv) > 5:
    db, ds, st = int(sys.argv[5], 16), int(sys.argv[6], 16), int(sys.argv[7], 16)
    data_end = max([a + l for n, a, l in secs if n in ('rwdata', 'bss')] + [db])
    free = db + ds - data_end
    ok = ok and free >= st and data_end >= db
    msg += ', data %04X-%04X (stack room %d, need %d)' % (db, data_end, free, st)
for l in open(mp):                          # variables addressed with direct page addressing
    m = re.match(r'Symbol: (kernel_dp_end|fsvars_end) \(\S+\) = ([0-9A-F]+)', l)
    if m and len(sys.argv) > 5 and int(m.group(2), 16) - int(sys.argv[5], 16) > 256:
        ok = False
        msg += '   (the direct page variables reach %s)' % m.group(2)
print(msg + ('' if ok else '   ** DOES NOT FIT **'))
sys.exit(0 if ok else 1)
