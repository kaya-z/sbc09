#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkfont.py - the 8x16 character ROM of the HDMI text display.

    mkfont.py [-o font8x16.hex] [-p preview.txt]

256 characters x 16 lines (the first line of a character is its top line), one byte per line,
bit 7 = the left pixel.  The glyphs are the Shinonome 16 dot fonts (vendor/shinonome, Public Domain):

    $20-$7E   ASCII (the ISO 8859-1 font)         $A1-$DF   half-width katakana (JIS X 0201)
    $A0, $E0-$FF  Latin-1 (accented letters)       $00-$1F, $7F-$9F  blank
"""
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'vendor', 'shinonome')


def read_bit(path):
    """a BDF with '@' and '.' bitmaps -> {encoding: [16 line bytes]}"""
    glyphs, enc, rows, inbm = {}, None, [], False
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        if line.startswith('ENCODING'):
            enc = int(line.split()[1])
        elif line.startswith('BITMAP'):
            inbm, rows = True, []
        elif line.startswith('ENDCHAR'):
            if enc is not None and len(rows) == 16:
                glyphs[enc] = rows
            inbm, enc = False, None
        elif inbm:
            s = line.strip()
            if re.fullmatch(r'[.@]{8}', s):
                rows.append(sum(1 << (7 - i) for i, c in enumerate(s) if c == '@'))
            else:
                inbm = False
    return glyphs


def build():
    latin = read_bit(os.path.join(SRC, 'latin1-16.bit'))
    kana = read_bit(os.path.join(SRC, 'hankaku-16.bit'))
    font = [[0] * 16 for _ in range(256)]
    for c in list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)):
        if c in latin:
            font[c] = latin[c]
    for c in range(0xa1, 0xe0):            # JIS X 0201 katakana over Latin-1
        font[c] = kana[c]
    return font


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('-o', '--output', default=os.path.join(HERE, '..', 'rtl', 'font8x16.hex'))
    ap.add_argument('-p', '--preview', help='write a text picture of the font')
    a = ap.parse_args()
    font = build()
    with open(a.output, 'w') as f:
        for g in font:
            for b in g:
                f.write('%02x\n' % b)
    if a.preview:
        with open(a.preview, 'w', encoding='utf-8') as f:
            for c in range(256):
                f.write('--- %02X\n' % c)
                for b in font[c]:
                    f.write(''.join('#' if b >> (7 - i) & 1 else '.' for i in range(8)) + '\n')
    print('%s: %d glyphs' % (a.output, sum(1 for g in font if any(g))))


if __name__ == '__main__':
    main()
