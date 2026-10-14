#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""hdmi_screen.py - read the picture of the HDMI text screen (fpga/tn9k, docs/FPGA.md) back into text.

The 640x480 picture (a PPM file written by the simulation test benches) has an 80 x 25 screen of 8x16 cells
(the character ROM is fpga/tn9k/rtl/font8x16.hex); every cell is matched with the glyphs of the font.
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, 'fpga', 'tn9k', 'rtl', 'font8x16.hex')
ROWS, COLS = 25, 80
Y0 = (480 - 16 * ROWS) // 2

_hex = open(FONT).read().split()
font = [bytes(int(l, 16) for l in _hex[i * 16:(i + 1) * 16]) for i in range(256)]


def palette(k):
    on, off = (0xff, 0x55) if k & 8 else (0xaa, 0x00)
    return ((on if k & 1 else off), (on if k & 2 else off), (on if k & 4 else off))


def read_ppm(path):
    d = open(path, 'rb').read()
    hdr, rest = d.split(b'255\n', 1)
    return rest


GL = {}
for c in range(256):
    GL[c] = font[c]


def decode(img):
    """-> cells[r][c] = (ch, fgrgb or None, bgrgb) ; ch = None if no glyph matches"""
    out = []
    for r in range(ROWS):
        row = []
        for c in range(COLS):
            px = [[img[((Y0 + r * 16 + y) * 640 + c * 8 + x) * 3:((Y0 + r * 16 + y) * 640 + c * 8 + x) * 3 + 3] for x in range(8)] for y in range(16)]
            cols = set(p for line in px for p in line)
            row.append(match_cell(px, cols))
        out.append(row)
    return out


def match_cell(px, cols):
    if len(cols) == 1:
        (p,) = cols
        return (0x20, None, tuple(p))
    if len(cols) != 2:
        return (None, None, None)
    best = None
    for c, g in GL.items():
        if not any(g):
            continue
        on = set(); off = set()
        for y in range(16):
            for x in range(8):
                (on if g[y] >> (7 - x) & 1 else off).add(px[y][x])
        if len(on) == 1 and len(off) == 1 and on != off:
            return (c, tuple(next(iter(on))), tuple(next(iter(off))))
    return (None, None, None)




def screen_lines(img):
    """the screen as 25 text lines: ASCII as it is, anything else '?', a cell with no glyph '#'"""
    out = []
    for row in decode(img):
        out.append(''.join('#' if c is None else (chr(c) if 0x20 <= c < 0x7f else '?') for c, _, _ in row).rstrip())
    return out


# ------------------------------------------------------------------ kanji
_rev = None


def _reverse_kanji():
    """glyph bytes (32) -> a character with that glyph"""
    global _rev
    if _rev is None:
        sys.path.insert(0, os.path.join(ROOT, 'fpga', 'tn9k', 'scripts'))
        import mkkanji
        _rev = {}
        by_idx = {}
        for cp, idx in mkkanji.unicode_map().items():
            if cp >= 0x100 and not (0xff61 <= cp <= 0xff9f):
                by_idx.setdefault(idx, cp)
        for idx, cp in by_idx.items():
            g = mkkanji.glyph(idx)
            if any(g):
                _rev.setdefault(g, chr(cp))
    return _rev


def _cell_bits(img, r, c):
    """the 16 lines of a cell as bytes, and its two colours (fg, bg) or None"""
    cols = set()
    px = []
    for y in range(16):
        base = ((Y0 + r * 16 + y) * 640 + c * 8) * 3
        px.append([bytes(img[base + 3 * x:base + 3 * x + 3]) for x in range(8)])
        cols.update(px[-1])
    return px, cols


def screen_unicode(img):
    """the screen as 25 lines of text with the kanji (level 1) as Unicode; '?' for a cell nothing matches"""
    rev = _reverse_kanji()
    dec = decode(img)
    lines = []
    for r in range(ROWS):
        line = []
        c = 0
        while c < COLS:
            ch, fg, bg = dec[r][c]
            if ch is not None:
                line.append(chr(ch) if 0x20 <= ch < 0x7f else (chr(0xff61 + ch - 0xa1) if 0xa1 <= ch <= 0xdf else '?'))
                c += 1
                continue
            # a wide character: this cell and the next one together
            if c + 1 < COLS:
                p1, c1 = _cell_bits(img, r, c)
                p2, c2 = _cell_bits(img, r, c + 1)
                cols = c1 | c2
                if len(cols) == 2:
                    # the foreground is the colour that is not the background; try both assignments
                    for on in cols:
                        key = bytearray()
                        for y in range(16):
                            for px in (p1, p2):
                                v = 0
                                for x in range(8):
                                    v = (v << 1) | (1 if px[y][x] == on else 0)
                                key.append(v)
                        if bytes(key) in rev:
                            line.append(rev[bytes(key)])
                            break
                    else:
                        line.append('?')
                    c += 2
                    continue
            line.append('?')
            c += 1
        lines.append(''.join(line).rstrip())
    return lines
