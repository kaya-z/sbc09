#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkl2disk.py [-s SECTORS] [-o OUT] [BASE.dsk]  - the disk of the Level 2 machine (the FPGA board's os9l2, or sim09 -2 without -v).

Starts from os9/OS9.dsk (the Level 1 disk), makes it larger (default 1440 sectors: DD.TOT and the allocation map) and
replaces the commands that have a Level 2 version (os9/level2/cmds: the ones that look at the memory and the system:
dir, mdir, mfree, procs, ident, jwp ...) by it.  The work is done by sim09 -2 itself (copy from /v0 to /d0, attr),
so sim09 has to be built.  The other commands (build, rename, display, tmode ...) are the same on both levels."""
import os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEC = 256


def main():
    args = sys.argv[1:]
    size, out = 1440, os.path.join(ROOT, 'os9', 'OS9L2.dsk')
    while args and args[0].startswith('-'):
        if args[0] == '-s': size = int(args[1])
        elif args[0] == '-o': out = args[1]
        else: sys.exit(__doc__)
        args = args[2:]
    base = args[0] if args else os.path.join(ROOT, 'os9', 'OS9.dsk')
    d = bytearray(open(base, 'rb').read())
    tot = int.from_bytes(d[0:3], 'big')
    if size > tot:
        d += bytes((size - tot) * SEC)
        old_map = int.from_bytes(d[4:6], 'big')
        d[0:3] = size.to_bytes(3, 'big')
        d[4:6] = (size // 8).to_bytes(2, 'big')
        d[SEC + old_map:SEC + size // 8] = bytes(size // 8 - old_map)   # the new sectors are free (the rest of the old map sector was $FF)                  # DD.MAP: bytes of the allocation map (one sector: 2048 sectors at most)
    open(out, 'wb').write(d)
    cdir = os.path.join(ROOT, 'os9', 'level2', 'cmds')
    names = []
    for f in sorted(os.listdir(cdir)):
        p = os.path.join(cdir, f)
        if os.path.isfile(p) and open(p, 'rb').read(2) == b'\x87\xcd' and f not in ('shutdown', ):
            names.append(f)
    names.append('shutdown')
    # only the commands that the disk has (the others are not on it): from the disk's own cmds directory
    have = "attr cmp copy date dhry del deldir dir dump echo free ident jwp link list load login makdir mdir mfree procs pwd pxd shutdown sleep tee tsmon unlink".split()
    names = [n for n in have if n in names or n == 'shutdown']
    lines = ['timeout 3000000', 'expect "OS9:"', 'send "chx /v0/cmds\\r"', 'expect "OS9:"']
    for n in names:
        lines += ['send "del /d0/cmds/%s\\r"' % n, 'expect "OS9:"', 'send "copy /v0/cmds/%s /d0/cmds/%s\\r"' % (n, n), 'expect "OS9:"',
                  'send "attr /d0/cmds/%s e pe r pr\\r"' % n, 'expect "OS9:"']
    lines += ['send "free /d0\\r"', 'expect "OS9:"', 'quit']
    with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as t:
        t.write('\n'.join(lines) + '\n')
    r = subprocess.run([os.path.join(ROOT, 'sim09'), '-2', '-max', '-rom', os.path.join(ROOT, 'os9', 'os9v2.rom'), '-0', out,
                        '-v', os.path.join(ROOT, 'os9', 'level2'), '-script', t.name], capture_output=True, env=dict(os.environ, LC_ALL='C'))
    os.unlink(t.name)
    txt = r.stdout.decode('latin1')
    bad = [l for l in txt.replace('\r', '').split('\n') if 'ERROR' in l.upper() and 'del' not in l.lower()]
    # the del of a command that is not there is an error we accept; a copy that fails is not
    cerr = [l for l in txt.replace('\r', '').split('\n') if l.startswith('copy:') or 'can\'t' in l]
    if cerr:
        sys.exit('mkl2disk: ' + '; '.join(cerr))
    free = [l for l in txt.replace('\r', '').split('\n') if 'Free sectors' in l or 'free sectors' in l]
    print('%s: %d sectors, %d commands replaced; %s' % (out, size, len(names), free[-1].strip() if free else ''))


main()
