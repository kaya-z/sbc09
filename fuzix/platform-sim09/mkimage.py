#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Convert the DECB style output of lwlink (records 00 len16 addr16 data..., FF 0000 exec16)
into the boot image of the sim09 boot ROM: a sequence of 256 byte sectors

    [addr hi][addr lo][n][n data bytes ...]     n = 1..253, the rest of the sector is zero
    [exec hi][exec lo][0]                       the last sector: jump to exec

Pieces that are all zero are not stored (the RAM is zero at power on)."""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
pos, chunks, entry = 0, [], None
while pos < len(data):
    t = data[pos]
    ln, addr = (data[pos + 1] << 8) | data[pos + 2], (data[pos + 3] << 8) | data[pos + 4]
    pos += 5
    if t == 0x00:
        chunks.append((addr, data[pos:pos + ln]))
        pos += ln
    elif t == 0xFF:
        entry = addr
        break
    else:
        sys.exit('bad record type %d at %d' % (t, pos - 5))
if entry is None:
    sys.exit('no entry record')
out = bytearray()
for addr, blk in chunks:
    for off in range(0, len(blk), 253):
        piece = blk[off:off + 253]
        if not any(piece):
            continue
        sec = bytearray(256)
        sec[0], sec[1], sec[2] = ((addr + off) >> 8) & 255, (addr + off) & 255, len(piece)
        sec[3:3 + len(piece)] = piece
        out += sec
end = bytearray(256)
end[0], end[1], end[2] = entry >> 8, entry & 255, 0
out += end
open(dst, 'wb').write(out)
lo = min(a for a, b in chunks)
hi = max(a + len(b) for a, b in chunks)
print('%s: %d sectors, entry %04X, memory %04X-%04X' % (dst, len(out) // 256, entry, lo, hi - 1))
