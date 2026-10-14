#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""relax.py - make the short branches of an lwasm source that do not reach into long branches.

    relax.py file.s [lwasm options]

lwasm does not extend branches by itself.  This assembles the file again and again and changes
the mnemonic (bsr -> lbsr, bne -> lbne, ...) on every line that lwasm reports as "Byte overflow",
until it assembles; then it tries to turn the long branches that it did not need back into short
ones.  (Only branches written in lower case at the start of the opcode field are touched.)"""
import re, subprocess, sys

fn = sys.argv[1]
opts = sys.argv[2:]
BR = {'bra', 'brn', 'bhi', 'bls', 'bcc', 'bhs', 'bcs', 'blo', 'bne', 'beq', 'bvc', 'bvs', 'bpl', 'bmi', 'bge', 'blt', 'bgt', 'ble', 'bsr'}


def asm(lines):
    open('/tmp/_relax.s', 'w').write(''.join(lines))
    p = subprocess.run(['lwasm', '-fobj', '-o', '/tmp/_relax.o'] + opts + ['/tmp/_relax.s'], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def mnem_pos(line):
    m = re.match(r'^((?:[A-Za-z_@][\w@.$]*)?)([ \t]+)([a-z]+)([ \t]|$)', line)
    return m


lines = open(fn, encoding='utf-8').readlines()
for it in range(50):
    rc, out = asm(lines)
    bad = sorted(set(int(x) for x in re.findall(r'\((\d+)\) : ERROR : Byte overflow', out)))
    if not bad:
        break
    for n in bad:
        m = mnem_pos(lines[n - 1])
        if not m or m.group(3) not in BR:
            sys.exit('relax: cannot fix line %d: %s' % (n, lines[n - 1]))
        lines[n - 1] = lines[n - 1][:m.start(3)] + 'l' + lines[n - 1][m.start(3):]
if rc:
    sys.exit(out)
# shorten what is not needed
changed = True
while changed:
    changed = False
    for i, l in enumerate(lines):
        m = mnem_pos(l)
        if m and m.group(3).startswith('lb') and m.group(3)[1:] in BR:
            old = lines[i]
            lines[i] = l[:m.start(3)] + l[m.start(3) + 1:]
            rc, _ = asm(lines)
            if rc:
                lines[i] = old
            else:
                changed = True
open(fn, 'w', encoding='utf-8').write(''.join(lines))
print('relax: %s assembles' % fn)
