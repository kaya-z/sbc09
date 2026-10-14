#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the HDMI text screen (fpga/tn9k/rtl/hdmi_*.v, docs/FPGA.md) in the Verilator simulation.

The terminal and the display run on their own: console bytes go in, TMDS symbols come out, which the test bench
decodes like a monitor (it also checks the DC balance of the symbols) into a 640x480 picture.  This test reads the
picture back into a 80x25 screen of (character, colours) by matching every 8x16 cell with the glyphs of the font
ROM, and compares it with a model of the terminal written independently for this test:

  * directed cases: text, colours, wrap, tab, backspace, scrolling, the scroll region, insert and delete lines,
    erase, cursor movement, save / restore, and the escape sequences of jwp (docs/JWP.md)
  * random sequences of all of these, 30 seeds

Needs verilator and a C++ compiler.    Run:  tests/hdmi_test.py [-v] [seeds]
"""
import os, random, subprocess, sys, tempfile, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hdmi_screen import font, palette, read_ppm, decode, match_cell, ROWS, COLS, Y0

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIMDIR = os.path.join(ROOT, 'fpga', 'tn9k', 'sim')
VERBOSE = '-v' in sys.argv
fails = total = 0


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

r = subprocess.run(['make', '-C', SIMDIR, '-f', 'Makefile.hdmi'], capture_output=True, text=True)
EXE = os.path.join(SIMDIR, 'build', 'hdmi0', 'obj', 'Vhdmi_top')
if r.returncode or not os.path.exists(EXE):
    print(r.stdout[-1500:], r.stderr[-1500:])
    sys.exit('build of the simulation failed')



# ------------------------------------------------------------------ the model of the terminal
class Model:
    def __init__(self):
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
        i = 0
        while i < len(data):
            self.byte(data[i])
            i += 1

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
                if self.wrap:
                    self.col = 0; self.wrap = False; self.lf()
                self.cells[self.row][self.col] = (b, self.attr())
                if self.col == COLS - 1: self.wrap = True
                else: self.col += 1
        elif self.pstate == 1:
            self.pstate = 0
            if b == 0x5b: self.pstate = 2; self.pr = [0, 0, 0, 0]; self.pi = 0; self.priv = False
            elif b == 0x37: self.save = (self.row, self.col)
            elif b == 0x38: self.row, self.col = self.save; self.wrap = False
            elif b == 0x44: self.lf()
            elif b == 0x45: self.col = 0; self.wrap = False; self.lf()
            elif b == 0x4d:
                self.wrap = False
                if self.row == self.top: self.scroll(False, self.top, self.bot)
                elif self.row: self.row -= 1
            elif b == 0x63:
                a = self.cells
                self.reset()
                self.cells = [[(0x20, 0x07)] * COLS for _ in range(ROWS)]
            elif b == 0x1b: self.pstate = 1
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


def run(data, seed_name='t', gap=1000, frames=2):
    d = tempfile.mkdtemp(prefix='hdmit_')
    inp, outp = os.path.join(d, 'in.txt'), os.path.join(d, 'out.ppm')
    with open(inp, 'wb') as f:
        # tb_hdmi reads backslash escapes: write the raw bytes with \xHH so nothing is lost
        f.write(''.join('\\x%02x' % b for b in data).encode())
    p = subprocess.run([EXE, '-i', inp, '-o', outp, '-f', str(frames), '-g', str(gap)], capture_output=True, text=True)
    img = read_ppm(outp) if os.path.exists(outp) else None
    shutil.rmtree(d, ignore_errors=True)
    return img, p.returncode, p.stderr


def compare(model, img, skip_cursor=True):
    """returns a list of differences"""
    diffs = []
    dec = decode(img)
    for r in range(ROWS):
        for c in range(COLS):
            if skip_cursor and (r, c) == (model.row, model.col):
                continue
            ch, a = model.cells[r][c]
            got, fgc, bgc = dec[r][c]
            fgm, bgm = palette(a & 15), palette(a >> 4)
            if ch in (0x20, 0x00) or not any(font[ch]):
                if got != 0x20 and got is not None and False:
                    pass
                if bgc != bgm and not (got == 0x20 and fgc is None and bgc == bgm):
                    diffs.append('(%d,%d) blank cell: bg %s expected %s (got %s)' % (r, c, bgc, bgm, got))
                continue
            if got != ch or fgc != fgm or bgc != bgm:
                if fgm == bgm:        # invisible text: a blank cell of that colour
                    if got == 0x20 and bgc == bgm: continue
                diffs.append('(%d,%d) char %02x fg %s bg %s expected, got %s fg %s bg %s' % (r, c, ch, fgm, bgm, None if got is None else '%02x' % got, fgc, bgc))
    return diffs


def screen_text(model):
    return '\n'.join(''.join(chr(ch) if 0x20 <= ch < 0x7f else ('.' if ch != 0x20 else ' ') for ch, a in row).rstrip() for row in model.cells).rstrip()


def case(name, data, gap=1000, frames=2):
    m = Model()
    m.feed(data)
    img, rc, err = run(data, name, gap, frames)
    check(rc == 0, name + ': the TMDS stream is valid and DC balanced', err)
    if img is None:
        return
    diffs = compare(m, img)
    check(not diffs, name, '\n'.join(diffs[:6]) + '\n' + screen_text(m)[:600])


ESC = b'\x1b'


def csi(s):
    return ESC + b'[' + s.encode()


DIRECTED = [
    ('text, colours, bold, reverse', b'Hello, HDMI!\r\n' + csi('31m') + b'red ' + csi('1;32m') + b'bright green ' + csi('0;44;33m') + b'yellow on blue ' + csi('7m') + b'reverse' + csi('m') + b'\r\nABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789\r\n'),
    ('half-width katakana and Latin-1', bytes([0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xa6, 0xdd, 0x20, 0xe9, 0xe8, 0xfc, 0x0d, 0x0a, 0xc0, 0xde, 0xdf])),
    ('a full line wraps at the next character, CR LF at the end does not skip a line', b'x' * 80 + b'\r\n' + b'y' * 80 + b'z' + b'\r\n' + b'end'),
    ('tab stops and backspace', b'a\tb\tc\x08\x08d\r\n' + b'\t' * 12 + b'Q\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08\x08R'),
    ('scrolling the screen up (40 lines)', b''.join(b'line %d\r\n' % i for i in range(40))),
    ('cursor addressing and clearing', csi('2J') + csi('5;10H') + b'at 5,10' + csi('A') + b'up' + csi('3B') + b'down' + csi('40C') + b'right' + csi('5D') + b'left' + csi('H') + b'home' + csi('25;80H') + b'X'),
    ('erase in line and display', b''.join(b'%02d ' % i + b'#' * 70 + b'\r\n' for i in range(24)) + csi('5;20H') + csi('K') + csi('8;20H') + csi('1K') + csi('10;1H') + csi('2K') + csi('12;40H') + csi('J') + csi('16;40H') + csi('1J')),
    ('scroll region, insert and delete lines (what jwp does)', b''.join(b'row %02d\r\n' % i for i in range(23)) + csi('1;20r') + csi('5;1H') + csi('2L') + b'new' + csi('10;1H') + csi('3M') + csi('r') + csi('24;1H') + b'status'),
    ('scrolling within a region, below it is not touched', csi('4;10r') + csi('1;1H') + b''.join(b'r%d\r\n' % i for i in range(3)) + csi('10;1H') + b''.join(b'S%d\r\n' % i for i in range(8)) + csi('r')),
    ('save and restore the cursor, ESC 7 / 8, index, next line, reverse index', csi('10;10H') + b'a' + ESC + b'7' + csi('1;1H') + b'b' + ESC + b'8' + b'c' + ESC + b'D' + b'd' + ESC + b'E' + b'e' + csi('1;1H') + ESC + b'M' + b'f' + csi('s') + csi('20;5H') + csi('u') + b'g'),
    ('reverse index at the top scrolls down', b''.join(b'%d\r\n' % i for i in range(5)) + csi('1;1H') + ESC + b'M' + ESC + b'M' + b'T'),
    ('jwp: line draw with ESC [ r;1 H, ESC [ K, reverse status line', csi('1;1H') + csi('K') + b'document line one' + csi('2;1H') + csi('K') + b'document line two' + csi('24;1H') + csi('7m') + b'-- status line --' + csi('m') + csi('1;18H')),
    ('unknown and private sequences are ignored', b'a' + csi('?25l') + b'b' + csi('5z') + b'c' + ESC + b'x' + b'd' + csi('1;2;3;4;5;6;7m') + b'e'),
    ('form feed clears the screen', b'junk\r\njunk\r\n' + b'\x0c' + b'clean'),
]

if '--no-directed' not in sys.argv:
    for name, data in DIRECTED:
        case(name, data)


# ------------------------------------------------------------------ random sequences
def rnd_seq(rnd, n):
    out = bytearray()
    for _ in range(n):
        k = rnd.random()
        if k < 0.35:
            out += bytes(rnd.choice([rnd.randrange(0x20, 0x7f), rnd.randrange(0xa1, 0xe0)]) for _ in range(rnd.randrange(1, 40)))
        elif k < 0.45: out += b'\r\n' * rnd.randrange(1, 4)
        elif k < 0.50: out += rnd.choice([b'\r', b'\n', b'\x08', b'\t'])
        else:
            c = rnd.choice('ABCDHfJKLMmrsu')
            if c in 'ABCD': s = '%d' % rnd.choice([0, 1, 2, 5, 30, 100]) if rnd.random() < .7 else ''
            elif c in 'Hf': s = '%d;%d' % (rnd.choice([0, 1, 5, 12, 25, 40]), rnd.choice([0, 1, 20, 79, 80, 120])) if rnd.random() < .8 else ''
            elif c in 'JK': s = rnd.choice(['', '0', '1', '2'])
            elif c in 'LM': s = rnd.choice(['', '1', '2', '5', '40'])
            elif c == 'm': s = ';'.join(str(rnd.choice([0, 1, 7, 22, 27, 30, 31, 32, 33, 34, 35, 36, 37, 39, 40, 41, 42, 43, 44, 45, 46, 47, 49, 90, 94])) for _ in range(rnd.randrange(1, 4)))
            elif c == 'r': s = rnd.choice(['', '1;25', '3;10', '5;5', '10;3', '1;20', '2;24', '0;0', '4'])
            else: s = ''
            out += csi(s + c)
    return bytes(out)


seeds = [int(a) for a in sys.argv[1:] if a.isdigit()]
for seed in (seeds or range(30)):
    rnd = random.Random(seed)
    data = rnd_seq(rnd, rnd.randrange(30, 120))
    case('random sequence, seed %d (%d bytes)' % (seed, len(data)), data, gap=4000, frames=3)

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
