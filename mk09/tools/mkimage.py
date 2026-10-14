#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkimage.py - make the boot image that the boot ROM loads from the disk.

    mkimage.py -o boot.img  entry-symbol-address  module.s19 [module.s19 ...]
    mkimage.py -o boot.img -e 0x0300 kernel.s19 tty.s19 ...

The boot ROM (boot/bootrom.s, the same format as the FUZIX boot ROM) reads 256 byte sectors
from sector 2 on.  Every sector is  [dest hi][dest lo][n][n data bytes]  (n = 1..253);  the sector
with n = 0 holds the entry address in the first two bytes and ends the loading.
The input is Motorola S-records (S1/S9 records, what lwlink --format=srec writes).
No segment may use the boot ROM's buffer page $0200-$02FF."""
import argparse, sys

ap = argparse.ArgumentParser()
ap.add_argument('-o', '--output', required=True)
ap.add_argument('-e', '--entry', required=True)
ap.add_argument('files', nargs='+')
a = ap.parse_args()

mem = {}
for fn in a.files:
    for line in open(fn):
        line = line.strip()
        if not line.startswith('S1'):
            continue
        n = int(line[2:4], 16)
        addr = int(line[4:8], 16)
        data = bytes.fromhex(line[8:8 + 2 * (n - 3)])
        for i, b in enumerate(data):
            if addr + i in mem and mem[addr + i] != b:
                sys.exit('mkimage: overlapping segments at %04X' % (addr + i))
            mem[addr + i] = b

for ad in mem:
    if 0x0200 <= ad < 0x0300:
        sys.exit('mkimage: data in the boot buffer page at %04X' % ad)

# contiguous runs
runs = []
for ad in sorted(mem):
    if runs and runs[-1][0] + len(runs[-1][1]) == ad:
        runs[-1][1].append(mem[ad])
    else:
        runs.append([ad, bytearray([mem[ad]])])

out = bytearray()
for start, data in runs:
    for off in range(0, len(data), 253):
        chunk = data[off:off + 253]
        ad = start + off
        out += bytes([ad >> 8, ad & 255, len(chunk)]) + chunk
        out += bytes(253 - len(chunk))
entry = int(a.entry, 0)
out += bytes([entry >> 8, entry & 255, 0]) + bytes(253)
open(a.output, 'wb').write(out)
print('boot image: %d sectors, %d bytes of code and data, entry %04X' % (len(out) // 256, len(mem), entry))
