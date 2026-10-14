#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkdisk.py - build the disk image of FUZIX for sim09.

    mkdisk.py -k fuzix.img -f rootfs.img [-s SWAPBLOCKS] -o sim09.dsk

Layout (the disk has 256 byte sectors for the simulator, 512 byte blocks for FUZIX):
    block 0        the partition table (PC style): partition 1 = root file system (type 0x7E),
                   partition 2 = swap (type 0x7F, found by the kernel by its type)
    sectors 2..    the kernel in the format of the boot ROM (mkimage.py), at most 1 MB
    block 2048..   partition 1: the file system image (build-filesystem, big endian)
    then           partition 2: the swap area (SWAPBLOCKS blocks, default 2048 = 1 MB)
"""
import argparse, struct, sys

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('-k', '--kernel', required=True, help='kernel image for the boot ROM (fuzix.img)')
ap.add_argument('-f', '--fs', required=True, help='file system image')
ap.add_argument('-s', '--swap', type=int, default=2048, help='size of the swap partition in 512 byte blocks')
ap.add_argument('-o', '--output', required=True)
a = ap.parse_args()

kern = open(a.kernel, 'rb').read()
fs = open(a.fs, 'rb').read()
if len(kern) + 512 > 2048 * 512:
    sys.exit('kernel image too large for the 1 MB boot area')
fs_blocks = (len(fs) + 511) // 512
mbr = bytearray(512)


def entry(i, typ, first, count):
    mbr[446 + 16 * i:446 + 16 * i + 16] = bytes([0, 0, 0, 0, typ, 0, 0, 0]) + struct.pack('<II', first, count)


entry(0, 0x7E, 2048, fs_blocks)
entry(1, 0x7F, 2048 + fs_blocks, a.swap)
mbr[510], mbr[511] = 0x55, 0xAA
img = bytearray(mbr) + kern
img += bytes(2048 * 512 - len(img))
img += fs + bytes(fs_blocks * 512 - len(fs)) + bytes(a.swap * 512)
open(a.output, 'wb').write(img)
print('%s: %d K (kernel %d sectors, root %d blocks, swap %d blocks)' % (a.output, len(img) // 1024, len(kern) // 256, fs_blocks, a.swap))
