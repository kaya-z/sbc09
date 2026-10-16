#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkl2disk.py [-s SECTORS] [-o OUT] [BASE.dsk]  - the disk of the Level 2 machine of the FPGA board (the same bitstream as FUZIX).

The 256 byte boot ROM of the bitstream loads the code from LSN 2 (as it does for FUZIX and MK09): the loader of
os9/level2/l2boot.asm (one record, LSN 2, and the entry record, LSN 3).  The loader switches the machine to the Level 2
mode, reads the ROM image of the Level 2 machine (os9/os9v2_tn9k.rom: 105 sectors, LSN 720-824, marked used in the
allocation map) into the physical memory and starts it.  The disk is /d0 of the Level 2 system.

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
    # the loader (a09 -o: no prefix, the code starts at $0300) and the ROM image
    ldr = os.path.join(tempfile.gettempdir(), 'l2boot.bin')
    r = subprocess.run([os.path.join(ROOT, 'tools', 'a09'), '-o', ldr, os.path.join(ROOT, 'os9', 'level2', 'l2boot.asm')], capture_output=True)
    code = open(ldr, 'rb').read()
    rom = open(os.path.join(ROOT, 'os9', 'os9v2_tn9k.rom'), 'rb').read()
    imglsn, nsect = 720, 105
    if len(rom) > nsect * SEC or len(code) > 253 or imglsn + nsect > size:
        sys.exit('mkl2disk: the ROM image (%d bytes) or the loader (%d bytes) does not fit: see os9/level2/l2boot.asm' % (len(rom), len(code)))
    d[2 * SEC:3 * SEC] = bytes([0x03, 0x00, len(code)]) + code + bytes(253 - len(code))        # [dest $0300][n][code]
    d[3 * SEC:4 * SEC] = bytes([0x03, 0x00, 0x00]) + bytes(253)                                  # [entry $0300][0]
    d[imglsn * SEC:imglsn * SEC + len(rom)] = rom
    for lsn in list(range(2, 4)) + list(range(imglsn, imglsn + nsect)):
        d[SEC + lsn // 8] |= 0x80 >> (lsn % 8)                                                    # used
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
