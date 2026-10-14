#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkkanji.py - the kanji image for the HDMI screen (docs/FPGA.md).

    mkkanji.py [-o kanji.bin] [--hex kanji.hex]

The glyphs are the Shinonome 16 dot JIS X 0208 font (vendor/shinonome, Public Domain): the symbols, digits,
Latin, hiragana, katakana, Greek and Cyrillic (rows 1-8) and the level 1 kanji (rows 16-47), 3760 slots
of 32 bytes.  The layout is in rtl/kanji.vh.  Besides, a table maps Unicode to a glyph number (for UTF-8).

This file is also the reference for the tests: glyph_index(), glyph(), unicode_index().
"""
import argparse, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'vendor', 'shinonome', 'kanji-16.bit')

KJ_DIR, KJ_PAGES, KJ_GLYPH = 0x100, 0x300, 0x10000
NSLOTS = 40 * 94
KJ_SIZE = KJ_GLYPH + NSLOTS * 32
GETA = None                                  # set below


def glyph_index(ku, ten):
    """compact glyph number of a JIS X 0208 row (1-94) and column (1-94); None when not in the image"""
    if 1 <= ku <= 8 and 1 <= ten <= 94:
        return (ku - 1) * 94 + ten - 1
    if 16 <= ku <= 47 and 1 <= ten <= 94:
        return (ku - 8) * 94 + ten - 1
    return None


GETA = glyph_index(2, 14)                    # JIS $222E


def read_glyphs():
    """-> {jis code: 32 bytes}"""
    glyphs, enc, rows, inbm = {}, None, [], False
    for line in open(SRC, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('ENCODING'):
            enc = int(line.split()[1])
        elif line.startswith('BITMAP'):
            inbm, rows = True, []
        elif line.startswith('ENDCHAR'):
            if enc is not None and len(rows) == 16:
                glyphs[enc] = b''.join(rows)
            inbm, enc = False, None
        elif inbm:
            s = line.strip()
            if len(s) == 16 and set(s) <= {'.', '@'}:
                v = sum(1 << (15 - i) for i, c in enumerate(s) if c == '@')
                rows.append(bytes([v >> 8, v & 255]))
            else:
                inbm = False
    return glyphs


_G = None


def glyph(idx):
    """the 32 bytes of the glyph with the compact number idx (blank when the font has none)"""
    global _G
    if _G is None:
        _G = read_glyphs()
    # idx -> ku, ten
    row, ten = divmod(idx, 94)
    ku = row + 1 if row < 8 else row + 8
    return _G.get(((ku + 0x20) << 8) | (ten + 1 + 0x20), bytes(32))


ALIASES = {0xff5e: (1, 33), 0x2225: (1, 34), 0xff0d: (1, 61), 0xffe0: (1, 81), 0xffe1: (1, 82), 0xffe2: (2, 44),
           0x2014: (1, 29), 0xffe5: (1, 79), 0x00a5: (1, 79), 0x203e: (1, 17)}


def unicode_map():
    """{code point: glyph number} for the rows in the image"""
    m = {}
    for ku in list(range(1, 9)) + list(range(16, 48)):
        for ten in range(1, 95):
            try:
                s = bytes([0xa0 + ku, 0xa0 + ten]).decode('euc_jp')
            except UnicodeDecodeError:
                continue
            if len(s) == 1:
                m[ord(s)] = glyph_index(ku, ten)
    for cp, (ku, ten) in ALIASES.items():
        m.setdefault(cp, glyph_index(ku, ten))
    return m


_U = None


def unicode_index(cp):
    global _U
    if _U is None:
        _U = unicode_map()
    return _U.get(cp)


def build():
    m = unicode_map()
    pages = {}
    for cp, idx in m.items():
        if cp < 0x100:
            continue                                          # ASCII / Latin-1 are in the 8x16 font
        pages.setdefault(cp >> 8, {})[cp & 255] = idx
    order = sorted(pages)
    assert len(order) * 512 + KJ_PAGES <= KJ_GLYPH
    img = bytearray(b'\xff' * KJ_SIZE)
    img[0:4] = b'KJF1'
    for hi in range(256):
        struct.pack_into('<H', img, KJ_DIR + 2 * hi, order.index(hi) if hi in pages else 0xffff)
    for n, hi in enumerate(order):
        base = KJ_PAGES + n * 512
        for lo in range(256):
            struct.pack_into('<H', img, base + 2 * lo, pages[hi].get(lo, 0xffff))
    for idx in range(NSLOTS):
        img[KJ_GLYPH + idx * 32:KJ_GLYPH + idx * 32 + 32] = glyph(idx)
    return bytes(img), len(order)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--output', default='kanji.bin')
    ap.add_argument('--hex', help='also write one byte per line, for $readmemh')
    ap.add_argument('--hex16', help='also write one 16 bit word (little endian) per line, for the PSRAM model of the simulation')
    a = ap.parse_args()
    img, npages = build()
    open(a.output, 'wb').write(img)
    if a.hex:
        with open(a.hex, 'w') as f:
            f.write(''.join('%02x\n' % b for b in img))
    if a.hex16:
        with open(a.hex16, 'w') as f:
            f.write(''.join('%04x\n' % (img[i] | img[i + 1] << 8) for i in range(0, len(img), 2)))
    nonblank = sum(1 for i in range(NSLOTS) if any(glyph(i)))
    print('%s: %d bytes, %d pages, %d glyphs, geta = %d' % (a.output, len(img), npages, nonblank, GETA))


if __name__ == '__main__':
    main()
