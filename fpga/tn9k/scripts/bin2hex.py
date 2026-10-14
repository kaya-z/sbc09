#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""bin2hex.py ROM.bin [SIZE] > rom.hex   one byte per line, for $readmemh (SIZE: pad up to this many bytes)"""
import sys
d = open(sys.argv[1], 'rb').read()
n = int(sys.argv[2], 0) if len(sys.argv) > 2 else len(d)
d = d.ljust(n, b'\xff')
sys.stdout.write(''.join('%02x\n' % b for b in d))
