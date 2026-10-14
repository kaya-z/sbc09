# SPDX-License-Identifier: MIT
"""fatlib.py - read an MK09 disk image (MBR + FAT16 volume) from Python, for the tests."""
import struct

BLK = 512


class Volume:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        mbr = self.data[:BLK]
        assert mbr[510:512] == b'\x55\xAA'
        pe = mbr[0x1BE:0x1BE + 16]
        self.start = struct.unpack_from('<I', pe, 8)[0]
        self.nblocks = struct.unpack_from('<I', pe, 12)[0]
        b = self.blk(0)
        (self.bps, self.spc, self.rsvd, self.nfats, self.rootents, tot16, _media, self.fatsz) = \
            struct.unpack_from('<HBHBHHBH', b, 11)
        self.rootsecs = (self.rootents * 32 + BLK - 1) // BLK
        self.fat_blk = self.rsvd
        self.root_blk = self.fat_blk + self.nfats * self.fatsz
        self.data_blk = self.root_blk + self.rootsecs
        self.ncl = (tot16 - self.data_blk) // self.spc
        fb = b''.join(self.blk(self.fat_blk + i) for i in range(self.fatsz))
        self.fat = struct.unpack('<%dH' % (len(fb) // 2), fb)

    def blk(self, n):
        o = (self.start + n) * BLK
        return self.data[o:o + BLK]

    def chain(self, cl):
        out = []
        while 2 <= cl < 0xFFF8:
            out.append(cl)
            cl = self.fat[cl]
        return out

    def read_chain(self, cl, size):
        out = b''
        for c in self.chain(cl):
            for i in range(self.spc):
                out += self.blk(self.data_blk + (c - 2) * self.spc + i)
        return out[:size]

    def entries(self, cl):
        if cl == 0:
            raw = b''.join(self.blk(self.root_blk + i) for i in range(self.rootsecs))
        else:
            raw = self.read_chain(cl, 1 << 30)
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 8:
                continue
            name = e[:8].decode('latin-1').rstrip() + ('.' + e[8:11].decode('latin-1').rstrip() if e[8:11].strip() else '')
            yield name, e[11], struct.unpack_from('<H', e, 26)[0], struct.unpack_from('<I', e, 28)[0]

    def find(self, path):
        cl, ent = 0, None
        up = ''.join(c.upper() if c < '\x80' else c for c in path)      # (only ASCII is case insensitive)
        parts = [p for p in up.split('/') if p]
        for i, p in enumerate(parts):
            for name, attr, c, size in self.entries(cl):
                if name == p:
                    ent = (name, attr, c, size)
                    cl = c
                    break
            else:
                return None
        return ent

    def read(self, path):
        e = self.find(path)
        if e is None:
            return None
        return self.read_chain(e[2], e[3])

    def listdir(self, path):
        e = self.find(path) if path.strip('/') else ('', 0x10, 0, 0)
        return [n for n, a, c, s in self.entries(e[2])] if e else None

    def write_volume(self, out):
        open(out, 'wb').write(self.data[self.start * BLK:(self.start + self.nblocks) * BLK])
