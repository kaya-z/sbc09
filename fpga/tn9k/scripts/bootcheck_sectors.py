#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""bootcheck_sectors.py DISK.dsk - the first two kernel sectors (LSN 2 and 3) as hex, the way scripts/bootcheck.s prints them"""
import sys
d = open(sys.argv[1], 'rb').read()
for lsn in (2, 3):
    print(''.join('%02X' % b for b in d[lsn * 256:(lsn + 1) * 256]))
