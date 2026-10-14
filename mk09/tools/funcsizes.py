#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""funcsizes.py - list the code size of every function of an lwlink map (from the symbol addresses)."""
import re, sys
syms = []
for l in open(sys.argv[1]):
    x = re.match(r'Symbol: (_\w+) \((\S+)\) = ([0-9A-F]+)', l)
    if x:
        syms.append((int(x.group(3), 16), x.group(1), x.group(2)))
secs = []
for l in open(sys.argv[1]):
    x = re.match(r'Section: code \((\S+)\) load at ([0-9A-F]+), length ([0-9A-F]+)', l)
    if x:
        secs.append((int(x.group(2), 16), int(x.group(3), 16), x.group(1)))
syms.sort()
res = []
for s, l, f in secs:
    inside = [(a, n) for a, n, ff in syms if ff == f and s <= a < s + l]
    for i, (a, n) in enumerate(inside):
        end = inside[i + 1][0] if i + 1 < len(inside) else s + l
        res.append((end - a, n, f))
res.sort(reverse=True)
for r in res[:int(sys.argv[2]) if len(sys.argv) > 2 else 25]:
    print('%5d %s %s' % r)
