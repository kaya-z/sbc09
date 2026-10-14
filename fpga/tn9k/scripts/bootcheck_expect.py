#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""bootcheck_expect.py DISK.dsk - the sums that scripts/bootcheck.s prints, from the kernel image on the disk image
(the sector format of fuzix/bootrom.s: [dest hi][dest lo][n][data] from LSN 2, the last sector has n = 0)."""
import sys
d = open(sys.argv[1], 'rb').read()
mem = bytearray(65536)
lsn = 2
while True:
    s = d[lsn * 256:(lsn + 1) * 256]
    dst, n = (s[0] << 8) | s[1], s[2]
    if n == 0:
        break
    for i in range(n):
        mem[(dst + i) & 0xffff] = s[3 + i]
    lsn += 1
out = []
for blk in range(1, 63):
    out.append('%04X' % (sum(mem[blk * 1024:(blk + 1) * 1024]) & 0xffff))
for i in range(0, len(out), 8):
    print(' '.join(out[i:i + 8]) + ' ')
