#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""os9edition.py MODULE EDITION - set the edition byte of an OS-9 module (the byte after its name) and fix the CRC.

A module made by CMOC has no edition byte (the CRC follows the name).  jwp gets one holding its version (1.0 -> 10, see
os9/jwp/Makefile), so that OS-9's `ident` tells the version of a jwp: the byte is inserted, the size in the header, the
header parity and the CRC are fixed."""
import sys


def crc24(data):
    crc = 0xFFFFFF
    for b in data:
        crc ^= b << 16
        for _ in range(8):
            crc <<= 1
            if crc & 0x1000000:
                crc ^= 0x800063
            crc &= 0xFFFFFF
    return ~crc & 0xFFFFFF


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, ed = sys.argv[1], int(sys.argv[2])
    m = bytearray(open(path, 'rb').read())
    if m[0:2] != b'\x87\xcd' or int.from_bytes(m[2:4], 'big') != len(m):
        sys.exit('os9edition: %s is not a single OS-9 module' % path)
    if crc24(m[:-3]).to_bytes(3, 'big') != bytes(m[-3:]):
        sys.exit('os9edition: the CRC of %s is wrong before the change' % path)
    p = int.from_bytes(m[4:6], 'big')                   # the name, the last character has bit 7 set
    while not m[p] & 0x80:
        p += 1
    if len(m) == p + 1 + 3:                             # no edition byte yet (CMOC): the CRC follows the name; insert one
        m[p + 1:p + 1] = bytes([ed])
        m[2:4] = len(m).to_bytes(2, 'big')
        x = 0
        for b in m[:8]:
            x ^= b
        m[8] = x ^ 0xFF                                 # header parity: the XOR of the first 9 bytes is $FF
    else:
        m[p + 1] = ed
    m[-3:] = crc24(m[:-3]).to_bytes(3, 'big')
    open(path, 'wb').write(m)
    print('%s: edition %d' % (path, ed))


main()
