#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the kanji display of the HDMI text screen (fpga/tn9k/rtl/hdmi_decode.v, hdmi_glyph.v, docs/FPGA.md).

The screen decodes the console bytes in UTF-8, EUC-JP, Shift_JIS or raw mode into 8x16 characters and 16x16 kanji
(JIS X 0208 symbols, hiragana, katakana, Greek, Cyrillic, level 1 kanji), reading the glyphs from the kanji image in the
PSRAM.  The simulation of the screen decodes the TMDS symbols into a 640x480 picture; this test draws the expected
picture itself (model of the terminal with wide characters, a model of the decoder, the glyphs of the font sources) and
compares all pixels:

  * the same Japanese text in all three encodings gives the same picture (and the model agrees with Python's codecs)
  * wide characters at the end of a line, written over one half of another, scrolled, inserted, erased
  * characters without a glyph (level 2 kanji, 4 byte UTF-8, JIS X 0212) are shown as a box; invalid sequences as "?"
  * ESC % G / E / S / @ switch the character set while running
  * random mixtures of text in the four character sets and escape sequences

Needs verilator.    Run:  tests/hdmi_kanji_test.py [-v] [seeds]
"""
import os, random, subprocess, sys, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'fpga', 'tn9k', 'scripts'))
import mkkanji
from hdmi_screen import font, palette, read_ppm, ROWS, COLS, Y0

SIMDIR = os.path.join(ROOT, 'fpga', 'tn9k', 'sim')
VERBOSE = '-v' in sys.argv
fails = total = 0
GETA = mkkanji.GETA


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     '))


if not shutil.which('verilator'):
    print('SKIP: verilator is not installed')
    sys.exit(0)

EXE = {}
for cs in (0, 1, 2, 3):
    r = subprocess.run(['make', '-C', SIMDIR, '-f', 'Makefile.hdmi', 'CHARSET=%d' % cs], capture_output=True, text=True)
    exe = os.path.join(SIMDIR, 'build', 'hdmi%d' % cs, 'obj', 'Vhdmi_top')
    if r.returncode or not os.path.exists(exe):
        print(r.stdout[-1500:], r.stderr[-1500:])
        sys.exit('build of the simulation failed')
    EXE[cs] = exe


# ------------------------------------------------------------------ the decoder (the model)
class Decoder:
    """raw 0, UTF-8 1, EUC-JP 2, Shift_JIS 3.  feed(byte) -> list of tokens: ('n', byte) narrow, ('w', glyph) wide"""

    def __init__(self, mode):
        self.mode = mode
        self.ms = 0
        self.esc = 0
        self.l1 = 0
        self.cp = 0
        self.big = False

    @staticmethod
    def jis(ku, ten):
        i = mkkanji.glyph_index(ku, ten) if (1 <= ku <= 94 and 1 <= ten <= 94) else None
        return ('w', GETA if i is None else i)

    def feed(self, b):
        out = []
        self._feed(b, out)
        return out

    def _bad(self, b, out):
        out.append(('n', 0x3f))
        self.ms = 0
        self._feed(b, out)

    def _feed(self, b, out):
        if b == 0x1b:
            if self.ms:
                return self._bad(b, out)
            self.esc = 1
            out.append(('n', b))
            return
        if self.esc and self.ms == 0 and b < 0x80:
            if self.esc == 1:
                self.esc = 2 if b == 0x25 else 0
            else:
                self.esc = 0
                m = {0x47: 1, 0x45: 2, 0x53: 3, 0x40: 0}.get(b)
                if m is not None:
                    self.mode = m
            out.append(('n', b))
            return
        if b < 0x80 and not (self.mode == 3 and self.ms == 1 and b >= 0x40):
            if self.ms:
                return self._bad(b, out)
            out.append(('n', b))
            return
        self.esc = 0
        m = self.mode
        if m == 0:
            out.append(('n', b))
        elif m == 1:
            if self.ms == 0:
                if 0xc2 <= b <= 0xdf: self.ms, self.cp, self.big = 1, b & 0x1f, False
                elif 0xe0 <= b <= 0xef: self.ms, self.cp, self.big = 2, b & 0x0f, False
                elif 0xf0 <= b <= 0xf7: self.ms, self.cp, self.big = 3, 0, True
                else: out.append(('n', 0x3f))
            elif 0x80 <= b <= 0xbf:
                self.cp = ((self.cp << 6) | (b & 0x3f)) & 0xffff
                self.ms -= 1
                if self.ms == 0:
                    out.append(self._ucs(self.cp))
            else:
                self._bad(b, out)
        elif m == 2:
            if self.ms == 0:
                if 0xa1 <= b <= 0xfe: self.l1, self.ms = b, 1
                elif b == 0x8e: self.ms = 2
                elif b == 0x8f: self.ms = 3
                else: out.append(('n', b))
            elif self.ms == 1:
                if 0xa1 <= b <= 0xfe:
                    self.ms = 0
                    out.append(self.jis(self.l1 - 0xa0, b - 0xa0))
                else: self._bad(b, out)
            elif self.ms == 2:
                if 0xa1 <= b <= 0xdf:
                    self.ms = 0
                    out.append(('n', b))
                else: self._bad(b, out)
            elif self.ms == 3:
                self.ms = 4
            else:
                self.ms = 0
                out.append(('w', GETA))
        else:
            if self.ms == 0:
                if 0x81 <= b <= 0x9f or 0xe0 <= b <= 0xfc: self.l1, self.ms = b, 1
                else: out.append(('n', b))
            elif 0x40 <= b <= 0x7e or 0x80 <= b <= 0xfc:
                a = self.l1 - (0xc1 if self.l1 >= 0xe0 else 0x81)
                t = b - (0x41 if b >= 0x80 else 0x40)
                self.ms = 0
                if t >= 94: out.append(self.jis(2 * a + 2, t - 93))
                else: out.append(self.jis(2 * a + 1, t + 1))
            else:
                self._bad(b, out)

    def _ucs(self, cp):
        if self.big:
            return ('w', GETA)
        if cp < 0x80 or 0xa0 <= cp < 0x100:
            return ('n', cp)
        if 0x80 <= cp < 0xa0:
            return None
        if 0xff61 <= cp <= 0xff9f:
            return ('n', cp - 0xff61 + 0xa1)
        i = mkkanji.unicode_index(cp)
        return ('w', GETA if i is None else i)


# ------------------------------------------------------------------ the terminal with wide characters (the model)
class Model:
    def __init__(self, mode):
        self.dec = Decoder(mode)
        self.reset()

    def reset(self):
        self.fg, self.bg, self.bold, self.rev = 7, 0, False, False
        self.row = self.col = 0
        self.wrap = False
        self.top, self.bot = 0, ROWS - 1
        self.save = (0, 0)
        self.pstate = 0
        self.cells = [[(0x20, 0x07)] * COLS for _ in range(ROWS)]

    def attr(self):
        f = (self.fg | 8) if self.bold and self.fg < 8 else self.fg
        return ((f << 4) | self.bg) if self.rev else ((self.bg << 4) | f)

    def blank_row(self, r):
        self.cells[r] = [(0x20, self.attr())] * COLS

    def scroll(self, up, r0, r1):
        if up:
            for r in range(r0, r1):
                self.cells[r] = self.cells[r + 1]
            self.blank_row(r1)
        else:
            for r in range(r1, r0, -1):
                self.cells[r] = self.cells[r - 1]
            self.blank_row(r0)

    def lf(self):
        self.wrap = False
        if self.row == self.bot:
            self.scroll(True, self.top, self.bot)
        elif self.row != ROWS - 1:
            self.row += 1

    def feed(self, data):
        for b in data:
            for tok in self.dec.feed(b):
                if tok is not None:
                    self.symbol(tok)

    def symbol(self, tok):
        kind, v = tok
        if kind == 'w':
            return self.putc(v, True)
        self.byte(v)

    def blank(self, r, c):
        self.cells[r][c] = (0x20, self.attr())

    def putc(self, v, wide):
        if wide:
            self.pstate = 0
        if self.wrap or (wide and self.col == COLS - 1):
            self.col = 0
            self.wrap = False
            self.lf()
        r, c = self.row, self.col
        old0 = self.cells[r][c][0] >> 12
        if old0 == 2 and c != 0:
            self.blank(r, c - 1)
        elif old0 == 1 and not wide and c != COLS - 1:
            self.blank(r, c + 1)
        if wide:
            old1 = self.cells[r][c + 1][0] >> 12
            if old1 == 1 and c + 2 <= COLS - 1:
                self.blank(r, c + 2)
            self.cells[r][c] = (0x1000 | v, self.attr())
            self.cells[r][c + 1] = (0x2000 | v, self.attr())
            if c + 1 == COLS - 1:
                self.col, self.wrap = COLS - 1, True
            else:
                self.col = c + 2
        else:
            self.cells[r][c] = (v, self.attr())
            if c == COLS - 1: self.wrap = True
            else: self.col += 1

    def byte(self, b):
        if self.pstate == 0:
            if b == 0x1b: self.pstate = 1
            elif b in (0x0a, 0x0b): self.lf()
            elif b == 0x0d: self.col = 0; self.wrap = False
            elif b == 0x08:
                if self.col: self.col -= 1
                self.wrap = False
            elif b == 0x09:
                self.col = COLS - 1 if (self.col >> 3) == ((COLS - 1) >> 3) else ((self.col >> 3) + 1) << 3
                self.wrap = False
            elif b == 0x0c:
                self.row = self.col = 0; self.wrap = False
                self.cells = [[(0x20, self.attr())] * COLS for _ in range(ROWS)]
            elif b >= 0x20 and b != 0x7f:
                self.putc(b, False)
        elif self.pstate == 1:
            self.pstate = 0
            if b == 0x5b: self.pstate = 2; self.pr = [0, 0, 0, 0]; self.pi = 0; self.priv = False
            elif b == 0x25: self.pstate = 3
            elif b == 0x37: self.save = (self.row, self.col)
            elif b == 0x38: self.row, self.col = self.save; self.wrap = False
            elif b == 0x44: self.lf()
            elif b == 0x45: self.col = 0; self.wrap = False; self.lf()
            elif b == 0x4d:
                self.wrap = False
                if self.row == self.top: self.scroll(False, self.top, self.bot)
                elif self.row: self.row -= 1
            elif b == 0x63:
                self.reset()
                self.cells = [[(0x20, 0x07)] * COLS for _ in range(ROWS)]
            elif b == 0x1b: self.pstate = 1
        elif self.pstate == 3:
            self.pstate = 0
        else:
            if 0x30 <= b <= 0x39:
                self.pr[self.pi] = min(255, self.pr[self.pi] * 10 + b - 0x30)
            elif b == 0x3b:
                if self.pi != 3: self.pi += 1
            elif b in (0x3f, 0x3c, 0x3d, 0x3e): self.priv = True
            elif 0x40 <= b <= 0x7e:
                self.pstate = 0
                self.csi(b)
            elif b == 0x1b: self.pstate = 1
            elif b in (0x18, 0x1a): self.pstate = 0

    def csi(self, c):
        if self.priv:
            return
        p = self.pr
        n1 = p[0] or 1
        ch = chr(c)
        if ch == 'A': self.row = self.row - n1 if (self.row >= n1 and n1 < 32) else 0; self.wrap = False
        elif ch == 'B': self.row = self.row + n1 if (n1 < 32 and self.row + n1 < ROWS) else ROWS - 1; self.wrap = False
        elif ch == 'C': self.col = self.col + n1 if (n1 < 128 and self.col + n1 < COLS) else COLS - 1; self.wrap = False
        elif ch == 'D': self.col = self.col - n1 if (self.col >= n1 and n1 < 128) else 0; self.wrap = False
        elif ch in 'Hf':
            self.row = 0 if p[0] == 0 else (ROWS - 1 if p[0] > ROWS else p[0] - 1)
            self.col = 0 if p[1] == 0 else (COLS - 1 if p[1] > COLS else p[1] - 1)
            self.wrap = False
        elif ch == 'J':
            a = self.attr()
            if p[0] == 0:
                for r in range(self.row, ROWS):
                    for cc in range(self.col if r == self.row else 0, COLS): self.cells[r][cc] = (0x20, a)
            elif p[0] == 1:
                for r in range(0, self.row + 1):
                    for cc in range(0, (self.col + 1) if r == self.row else COLS): self.cells[r][cc] = (0x20, a)
            else:
                self.cells = [[(0x20, a)] * COLS for _ in range(ROWS)]
        elif ch == 'K':
            a = self.attr()
            lo, hi = (self.col, COLS) if p[0] == 0 else ((0, self.col + 1) if p[0] == 1 else (0, COLS))
            for cc in range(lo, hi): self.cells[self.row][cc] = (0x20, a)
        elif ch in 'LM':
            if self.top <= self.row <= self.bot:
                for _ in range(min(n1, self.bot - self.row + 1)):
                    self.scroll(ch == 'M', self.row, self.bot)
                self.col = 0; self.wrap = False
        elif ch == 'm':
            for k in p[:self.pi + 1]:
                if k == 0: self.fg, self.bg, self.bold, self.rev = 7, 0, False, False
                elif k == 1: self.bold = True
                elif k == 22: self.bold = False
                elif k == 7: self.rev = True
                elif k == 27: self.rev = False
                elif 30 <= k <= 37: self.fg = k - 30
                elif k == 39: self.fg = 7
                elif 40 <= k <= 47: self.bg = k - 40
                elif k == 49: self.bg = 0
                elif 90 <= k <= 97: self.fg = k - 90; self.bold = True
        elif ch == 'r':
            if p[0] == 0 and p[1] == 0: self.top, self.bot = 0, ROWS - 1
            else:
                a, b = (p[0] or 1), (p[1] or ROWS)
                if a < b <= ROWS: self.top, self.bot = a - 1, b - 1
            self.row = self.col = 0; self.wrap = False
        elif ch == 's': self.save = (self.row, self.col)
        elif ch == 'u': self.row, self.col = self.save; self.wrap = False


# ------------------------------------------------------------------ the expected picture
def render(m):
    img = bytearray(640 * 480 * 3)
    for r in range(ROWS):
        for c in range(COLS):
            code, at = m.cells[r][c]
            fg, bg = palette(at & 15), palette(at >> 4)
            if code < 0x100:
                rows = [font[code][y] for y in range(16)]
            else:
                g = mkkanji.glyph(code & 0xfff)
                rows = [g[2 * y + (0 if code >> 12 == 1 else 1)] for y in range(16)]
            for y in range(16):
                bits = rows[y]
                base = ((Y0 + r * 16 + y) * 640 + c * 8) * 3
                for x in range(8):
                    img[base + 3 * x:base + 3 * x + 3] = bytes(fg if bits >> (7 - x) & 1 else bg)
    return img


def diff_cells(m, img, skip_cursor=True):
    exp = render(m)
    out = []
    for r in range(ROWS):
        for c in range(COLS):
            if skip_cursor and (r, c) == (m.row, m.col):
                continue
            for y in range(16):
                base = ((Y0 + r * 16 + y) * 640 + c * 8) * 3
                if exp[base:base + 24] != img[base:base + 24]:
                    out.append('(%d,%d) cell %04x attr %02x differs at line %d' % (r, c, m.cells[r][c][0], m.cells[r][c][1], y))
                    break
    return out


def run(cs, data, gap=2500, frames=3):
    d = tempfile.mkdtemp(prefix='hdmik_')
    inp, outp = os.path.join(d, 'in.txt'), os.path.join(d, 'out.ppm')
    with open(inp, 'wb') as f:
        f.write(''.join('\\x%02x' % b for b in data).encode())
    p = subprocess.run([EXE[cs], '-i', inp, '-o', outp, '-f', str(frames), '-g', str(gap)], capture_output=True, text=True)
    img = read_ppm(outp) if os.path.exists(outp) else None
    shutil.rmtree(d, ignore_errors=True)
    return img, p.returncode, p.stderr


def case(name, cs, data, mode=None, gap=2500, frames=3):
    m = Model(cs if mode is None else mode)
    m.feed(data)
    img, rc, err = run(cs, data, gap, frames)
    check(rc == 0, name + ': valid TMDS', err)
    if img is None:
        return None
    d = diff_cells(m, img)
    check(not d, name, '\n'.join(d[:5]) + '\n' + screen(m)[:500])
    return m


def screen(m):
    def ch(code):
        if code < 0x100: return chr(code) if 0x20 <= code < 0x7f else ('.' if code != 0x20 else ' ')
        return '[' if code >> 12 == 1 else ']'
    return '\n'.join(''.join(ch(c) for c, a in row).rstrip() for row in m.cells).rstrip()


ESC = b'\x1b'
def csi(s): return ESC + b'[' + s.encode()


TEXT1 = '日本語のテキスト、ひらがなとカタカナ。\r\n半角ｶﾀｶﾅ ABC １２３ ＡＢＣ αβγ ДЖЗ ①②③ ㈱\r\n漢字は第一水準だけ：亜唖娃阿哀愛挨姶逢葵茜穐悪握渥旭葦芦鯵梓圧斡扱宛姐虻飴絢綾鮎或粟袷安庵按暗案闇鞍杏\r\n'
ENC = {0: 'cp932', 1: 'utf-8', 2: 'euc_jp', 3: 'cp932'}


def enc(s, cs):
    return s.encode(ENC[cs], 'replace')


# ------------------------------------------------------------------ the model of the decoder against Python's codecs
def codec_tokens(s, cs):
    """the tokens Python's codecs give for a string that the encoding can represent"""
    out = []
    for ch in s:
        cp = ord(ch)
        if cp < 0x80 or 0xa0 <= cp < 0x100: out.append(('n', cp))
        elif 0xff61 <= cp <= 0xff9f: out.append(('n', cp - 0xff61 + 0xa1))
        else:
            i = mkkanji.unicode_index(cp)
            out.append(('w', GETA if i is None else i))
    return out


for cs in (1, 2, 3):
    s = TEXT1.replace('\r\n', '\n') + '＠＃％＆ー〜～ｱｲｳ髙﨑'
    ok = True
    b = []
    for ch in s:
        try:
            e = ch.encode({1: 'utf-8', 2: 'euc_jp', 3: 'cp932'}[cs])
        except UnicodeEncodeError:
            continue
        d = Decoder(cs)
        toks = []
        for x in e: toks += [t for t in d.feed(x) if t]
        want = codec_tokens(ch, cs)
        if len(toks) != 1 or toks[0] != want[0]:
            ok = False
            b.append('%r %s -> %s, codec says %s' % (ch, e.hex(), toks, want))
    check(ok, 'decoder model vs Python codecs, character set %d' % cs, '\n'.join(b[:5]))


# ------------------------------------------------------------------ directed cases
for cs in (1, 2, 3):
    case('Japanese text, character set %d' % cs, cs, enc(TEXT1, cs))

# the three encodings give the same picture
TEXT2 = TEXT1.replace(' ①②③ ㈱', '')            # what all three encodings can represent
imgs = [run(cs, enc(TEXT2, cs))[0] for cs in (1, 2, 3)]
check(imgs[0] is not None and imgs[0] == imgs[1] == imgs[2], 'UTF-8, EUC-JP and Shift_JIS: the pictures are identical')

for cs in (1, 2, 3):
    e = lambda s: enc(s, cs)
    case('wide characters at the end of a line (cs %d)' % cs, cs, e('x' * 78 + '漢字' + 'y' * 77 + '日本' + '\r\n' + 'a' * 79 + '本' + '\r\n' + 'b' * 76 + '漢字漢'))
    case('writing over the halves of wide characters (cs %d)' % cs, cs,
         e('漢字漢字漢字\r') + b'A' + e('\r') + csi('1;2H') + b'B' + e('\r\n') + e('漢字漢字漢字\r\n') + csi('2;3H') + e('日') + csi('2;8H') + e('本') +
         csi('1;1H') + e('あ') + csi('1;4H') + e('い') + csi('1;12H') + b'z')
    case('scrolling and inserting lines with wide characters (cs %d)' % cs, cs,
         b''.join(e('行%02d：漢字とかな\r\n' % i) for i in range(30)) + csi('3;1H') + csi('2L') + e('挿入') + csi('10;1H') + csi('3M') + csi('20;24r') + csi('24;1H') + b'\n' + e('領域'))
    case('erasing next to wide characters (cs %d)' % cs, cs,
         e('漢字漢字漢字漢字\r\n漢字漢字漢字漢字\r\n漢字漢字漢字漢字\r\n') + csi('1;5H') + csi('K') + csi('2;4H') + csi('1K') + csi('3;1H') + csi('2K') +
         csi('1;1H') + csi('31;44m') + e('色つき') + csi('0m') + csi('7m') + e('反転') + csi('m'))
    case('characters without a glyph: a box (cs %d)' % cs, cs,
         e('level 2: 鬱 躾 &nbsp; 𠮟 😀 end\r\n') if cs == 1 else (b'ab\xb0\xa1\xb4\xa1\x8f\xb0\xa1c\r\n' if cs == 2 else b'ab\x88\x9f\x98\x9f\xfc\xfcc\r\n'))

# invalid sequences
inv = {
    1: b'a\x80b\xe3\x81c\xe3\x81\x82\xc3d\xf0\x9f\x98\x80e\xff\r\n',
    2: b'a\xa1b\xa1\xa1\xa1\x20c\x8e\x20d\x8f\xa1\xa1e\xfe\xfe\r\n',
    3: b'a\x81b\x81\x40\x81\x20c\x88\x9f\xfd\xfe\r\n',
}
for cs, d in inv.items():
    case('invalid sequences give "?" and the next byte is read again (cs %d)' % cs, cs, d)

# switching the character set while running (the raw build starts raw)
sw = (b'raw: ' + bytes([0xb1, 0xb2, 0xb3]) + b'\r\n' + ESC + b'%G' + 'UTF-8: 日本語\r\n'.encode() + ESC + b'%E' + 'EUC: 日本語\r\n'.encode('euc_jp') +
      ESC + b'%S' + 'SJIS: 日本語\r\n'.encode('cp932') + ESC + b'%@' + b'raw again: ' + bytes([0xb1]) + b'\r\n')
case('ESC % G / E / S / @ switch the character set (raw start)', 0, sw)
case('ESC % G / E / S / @ switch the character set (UTF-8 start)', 1, sw)

# jwp style
jw = b''.join(csi('%d;1H' % (r + 1)) + csi('K') + enc('これは日本語の文書の第%d行です。' % r, 1) for r in range(1, 23)) + csi('24;1H') + csi('7m') + enc('-- 状態行 --', 1) + csi('m') + csi('1;1H')
case('jwp: lines drawn with cursor addressing', 1, jw)


# ------------------------------------------------------------------ random
POOL = list('abcxyzABCXYZ 0123456789.,:;!?-+*/()[]') + list('日本語漢字水準亜唖娃阿哀愛あいうえおかきくけこアイウエオカキクケコ') + \
       list('ＡＢＣ１２３、。「」ー〜αβγΩДЖЗ①②㈱') + list('ｱｲｳｴｵｶﾀｶﾅ') + list('鬱躾髙')


def rnd_data(rnd, n, cs):
    out = bytearray()
    for _ in range(n):
        k = rnd.random()
        if k < 0.55:
            s = ''.join(rnd.choice(POOL) for _ in range(rnd.randrange(1, 25)))
            for ch in s:
                try:
                    out += ch.encode({1: 'utf-8', 2: 'euc_jp', 3: 'cp932', 0: 'cp932'}[cs])
                except UnicodeEncodeError:
                    out += b'?'
        elif k < 0.65: out += b'\r\n' * rnd.randrange(1, 3)
        elif k < 0.70: out += rnd.choice([b'\r', b'\n', b'\x08', b'\t'])
        else:
            c = rnd.choice('ABCDHfJKLMmrsu')
            if c in 'ABCD': s = '%d' % rnd.choice([0, 1, 2, 5, 30]) if rnd.random() < .7 else ''
            elif c in 'Hf': s = '%d;%d' % (rnd.choice([0, 1, 5, 12, 25]), rnd.choice([0, 1, 20, 78, 79, 80])) if rnd.random() < .8 else ''
            elif c in 'JK': s = rnd.choice(['', '0', '1', '2'])
            elif c in 'LM': s = rnd.choice(['', '1', '2', '5'])
            elif c == 'm': s = ';'.join(str(rnd.choice([0, 1, 7, 22, 27, 31, 32, 34, 37, 41, 44, 47, 49, 90])) for _ in range(rnd.randrange(1, 3)))
            elif c == 'r': s = rnd.choice(['', '1;25', '3;10', '2;24', '0;0'])
            else: s = ''
            out += csi(s + c)
    return bytes(out)


seeds = [int(a) for a in sys.argv[1:] if a.isdigit()]
for seed in (seeds or range(12)):
    for cs in (1, 2, 3):
        rnd = random.Random(seed * 10 + cs)
        data = rnd_data(rnd, rnd.randrange(20, 70), cs)
        case('random sequence, seed %d, character set %d (%d bytes)' % (seed, cs, len(data)), cs, data, gap=3500)

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
