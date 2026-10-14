#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 sim09 contributors
"""mkboot.py DISK BOOTFILE   - put the OS-9 boot file (OS9Boot) onto a disk image.

The disk must have an OS9Boot file in the root directory (os9/OS9.dsk has one).  The boot file takes the place of its
data: it is written to the sectors the file occupies (it must fit), the size in its file descriptor and DD.BSZ in LSN 0
are set, and DD.BT is made to point at the first sector.  The booter of os9v1boot.rom (os9/level1/boot.asm) reads the
file from DD.BT, DD.BSZ bytes.  A boot file in several pieces is not supported.
"""
import sys

SEC = 256


def be(b):
    return int.from_bytes(b, 'big')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    disk = bytearray(open(sys.argv[1], 'rb').read())
    boot = open(sys.argv[2], 'rb').read()
    lsn0 = disk[0:SEC]
    dd_dir = be(lsn0[8:11])
    sec = lambda n: disk[n * SEC:(n + 1) * SEC]

    def segments(fd):                                   # (first sector, count) of a file descriptor, FD.SEG at +16, 5 bytes each
        out = []
        for i in range(16, 256 - 4, 5):
            lsn, cnt = be(fd[i:i + 3]), be(fd[i + 3:i + 5])
            if cnt == 0:
                break
            out.append((lsn, cnt))
        return out

    def dir_data(fd):
        size = be(fd[9:13])
        data = b''.join(bytes(disk[l * SEC:(l + c) * SEC]) for l, c in segments(fd))
        return data[:size]

    root = dir_data(sec(dd_dir))
    fdlsn = None
    for off in range(0, len(root), 32):
        e = root[off:off + 32]
        name = bytes(e[:29])
        end = next((i for i, c in enumerate(name) if c & 0x80), None)
        if end is not None and (bytes(name[:end]) + bytes([name[end] & 0x7f])) == b'OS9Boot':
            fdlsn = be(e[29:32])
            break
    if fdlsn is None:
        sys.exit('mkboot: no OS9Boot file in the root directory of ' + sys.argv[1])
    fd = bytearray(sec(fdlsn))
    seg = segments(fd)
    if len(seg) != 1:
        sys.exit('mkboot: the OS9Boot file of the disk is in %d pieces (one is needed)' % len(seg))
    lsn, cnt = seg[0]
    if len(boot) > cnt * SEC:
        sys.exit('mkboot: the boot file is %d bytes, the OS9Boot file of the disk holds %d' % (len(boot), cnt * SEC))
    disk[lsn * SEC:(lsn + cnt) * SEC] = boot + bytes(cnt * SEC - len(boot))
    fd[9:13] = len(boot).to_bytes(4, 'big')
    disk[fdlsn * SEC:(fdlsn + 1) * SEC] = fd
    disk[21:24] = lsn.to_bytes(3, 'big')                # DD.BT
    disk[24:26] = len(boot).to_bytes(2, 'big')          # DD.BSZ
    open(sys.argv[1], 'wb').write(disk)
    print('boot file: %d bytes at LSN %d (room for %d)' % (len(boot), lsn, cnt * SEC))


main()
