#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkkernel.py [-b BASE] [-e LSN] DISK MODULE...   - put an OS-9 Level 1 kernel on a disk image, to be loaded by the
256 byte boot ROM of FUZIX / MK09 (fuzix/bootrom.s).

The modules (Boot, Init, IOMan, SCF, KrnP2, Krn: the last one is the kernel, its entry point is where the boot ROM jumps
to) are laid out one after the other in RAM from BASE (default $E100: above the RAM the kernel hands out, below the
boot ROM at $FF00; the kernel validates the modules it finds there, like the "boot track" of a CoCo).  They are written
to the disk as the boot ROM wants them, from LSN 2 on: sectors of  [dest hi][dest lo][n][n bytes]  and one last sector
[entry hi][entry lo][0].  The boot ROM puts the hardware vectors on the jump table at $0100, where the OS-9 Level 1
kernel installs its own (D.XSWI3 ... D.XFIRQ, in the same order).

The disk needs the sectors: LSN 2-17 (the kernel track of the original disk).  An image longer than 16 sectors goes on
at LSN -e (a sector  [lsn hi][lsn lo][$FF]  of the first part tells the boot ROM to go there); the sectors are marked
used in the allocation map (the disk is made larger when -e is beyond its end)."""
import sys

SEC = 256


def be(b):
    return int.from_bytes(b, 'big')


def main():
    args = sys.argv[1:]
    base, ext = 0xE100, None
    while args and args[0].startswith('-'):
        if args[0] == '-b':
            base = int(args[1], 0)
        elif args[0] == '-e':
            ext = int(args[1], 0)
        else:
            sys.exit(__doc__)
        args = args[2:]
    if len(args) < 2:
        sys.exit(__doc__)
    disk = bytearray(open(args[0], 'rb').read())
    blob = bytearray()
    entry = None
    for f in args[1:]:
        m = open(f, 'rb').read()
        if m[0:2] != b'\x87\xcd' or be(m[2:4]) > len(m) or (f != args[-1] and be(m[2:4]) != len(m)):
            sys.exit('mkkernel: %s is not an OS-9 module (only the kernel, the last one, may have bytes after its module: its vectors)' % f)
        entry = base + len(blob) + be(m[9:11])           # the last module is the kernel: its exec offset
        blob += m
    if base + len(blob) > 0xFF00:
        sys.exit('mkkernel: the modules are %d bytes, from $%04X they end at $%04X (above $FEFF)' % (len(blob), base, base + len(blob)))
    # the records: 253 bytes per sector
    secs = []
    for i in range(0, len(blob), 253):
        chunk = blob[i:i + 253]
        secs.append(bytes([(base + i) >> 8, (base + i) & 0xFF, len(chunk)]) + chunk + bytes(253 - len(chunk)))
    secs.append(bytes([entry >> 8, entry & 0xFF, 0]) + bytes(253))
    first = list(range(2, 18))                           # LSN 2-17
    places = []                                          # (lsn, sector)
    if len(secs) <= len(first):
        places = list(zip(first, secs))
    else:
        if ext is None:
            sys.exit('mkkernel: %d sectors do not fit into LSN 2-17: give -e LSN for the rest' % len(secs))
        head = secs[:len(first) - 1]
        tail = secs[len(first) - 1:]
        places = list(zip(first, head)) + [(first[-1], bytes([ext >> 8, ext & 0xFF, 0xFF]) + bytes(253))]
        places += [(ext + k, s) for k, s in enumerate(tail)]
    need = max(l for l, _ in places) + 1
    tot = be(disk[0:3])
    if need > tot:                                       # make the disk larger: DD.TOT and the allocation map
        newtot = (need + 7) // 8 * 8
        if newtot // 8 > 255 or newtot // 8 > SEC - 0:
            sys.exit('mkkernel: the allocation map (one sector) cannot hold %d sectors' % newtot)
        disk += bytes((newtot - tot) * SEC)
        disk[0:3] = newtot.to_bytes(3, 'big')
        disk[4:6] = (newtot // 8).to_bytes(2, 'big')     # DD.MAP: bytes of the map
        tot = newtot
    bm = 1 * SEC
    for lsn, s in places:
        disk[lsn * SEC:(lsn + 1) * SEC] = s
        disk[bm + lsn // 8] |= 0x80 >> (lsn % 8)          # mark it used
    open(args[0], 'wb').write(disk)
    print('kernel: %d bytes of modules at $%04X-$%04X, entry $%04X, %d sectors (LSN 2-17%s)' %
          (len(blob), base, base + len(blob) - 1, entry, len(secs), (', %d-%d' % (ext, ext + len(secs) - len(first))) if ext is not None and len(secs) > len(first) else ''))


main()
