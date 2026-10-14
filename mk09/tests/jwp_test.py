#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the Japanese word processor jwp (mk09/apps/jwp) running on MK09 in the simulator.

jwp is driven through the simulator's expect/send script engine; its escape
sequence output is fed into a small terminal emulator (80x24, East Asian
wide characters take two cells) so the tests can check what the user would
see.   Run:  tests/jwp_test.py [-v]
"""
import os, re, subprocess, sys, tempfile, shutil, unicodedata

sys.stdout.reconfigure(encoding='utf-8')

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
SIM = os.path.join(ROOT, 'sim09')
sys.path.insert(0, HERE)
VERBOSE = '-v' in sys.argv
fails = 0
total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    if not cond:
        fails += 1
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     '))


class Screen:
    """minimal ANSI terminal: CUP, EL, ED, SGR (reverse tracked)"""

    def __init__(self, cols=80, rows=24):
        self.cols, self.rows = cols, rows
        self.cells = [[' '] * cols for _ in range(rows)]
        self.rev = [[False] * cols for _ in range(rows)]
        self.r = self.c = 0
        self.attr = False
        self.top, self.bot = 0, rows - 1        # scrolling region (DECSTBM)

    def feed(self, text):
        i = 0
        n = len(text)
        while i < n:
            ch = text[i]
            if ch == '\x1b' and i + 1 < n and text[i + 1] == '[':
                j = i + 2
                while j < n and (text[j].isdigit() or text[j] in ';?'):
                    j += 1
                if j >= n:
                    break
                par, fin = text[i + 2:j], text[j]
                nums = [int(x) if x else 0 for x in par.split(';')] if par else []
                if fin == 'H':
                    self.r = (nums[0] - 1 if len(nums) > 0 and nums[0] else 0)
                    self.c = (nums[1] - 1 if len(nums) > 1 and nums[1] else 0)
                elif fin == 'K':
                    for x in range(self.c, self.cols):
                        self.cells[self.r][x] = ' '
                        self.rev[self.r][x] = False
                elif fin == 'J':
                    self.cells = [[' '] * self.cols for _ in range(self.rows)]
                    self.rev = [[False] * self.cols for _ in range(self.rows)]
                elif fin == 'm':
                    self.attr = bool(nums and 7 in nums)
                elif fin == 'r':
                    self.top = (nums[0] - 1) if len(nums) > 0 and nums[0] else 0
                    self.bot = (nums[1] - 1) if len(nums) > 1 and nums[1] else self.rows - 1
                    self.r = self.c = 0
                elif fin in 'LM' and self.top <= self.r <= self.bot:          # IL / DL inside the region
                    n_ = nums[0] if nums and nums[0] else 1
                    n_ = min(n_, self.bot - self.r + 1)
                    blank = [' '] * self.cols
                    for _ in range(n_):
                        if fin == 'L':
                            self.cells.pop(self.bot)
                            self.rev.pop(self.bot)
                            self.cells.insert(self.r, list(blank))
                            self.rev.insert(self.r, [False] * self.cols)
                        else:
                            self.cells.pop(self.r)
                            self.rev.pop(self.r)
                            self.cells.insert(self.bot, list(blank))
                            self.rev.insert(self.bot, [False] * self.cols)
                i = j + 1
                continue
            if ch == '\r':
                self.c = 0
            elif ch == '\n':
                self.r = min(self.r + 1, self.rows - 1)
            elif ch == '\b':
                self.c = max(0, self.c - 1)
            elif ch >= ' ':
                w = 2 if unicodedata.east_asian_width(ch) in 'WF' else 1
                if self.c + w > self.cols:
                    self.c = 0
                    self.r = min(self.r + 1, self.rows - 1)
                self.cells[self.r][self.c] = ch
                self.rev[self.r][self.c] = self.attr
                if w == 2:
                    self.cells[self.r][self.c + 1] = ''
                self.c += w
            i += 1

    def line(self, r):
        return ''.join(self.cells[r]).rstrip()

    def lines(self):
        return [self.line(r) for r in range(self.rows)]


BUILDS = {'utf8': os.path.join(MK, os.environ.get('MK09_BUILD', 'build')), 'euc': os.path.join(MK, 'build_euc'), 'sjis': os.path.join(MK, 'build_sjis')}
MKDISK = os.path.join(MK, 'tools', 'mkdisk.py')


def jwp_bufsz(build):
    m = re.search(r'JWP_BUF\s*=\s*(\d+)', open(os.path.join(build, 'jwp.bufsz')).read()) if os.path.exists(os.path.join(build, 'jwp.bufsz')) else None
    return int(m.group(1)) if m else 0


def _charsets():
    return [(k, c) for k, c in (('euc', 'euc_jp'), ('sjis', 'cp932')) if os.path.exists(os.path.join(BUILDS[k], 'tree', 'bin', 'jwp.mx'))]


CHARSETS = _charsets()
if not CHARSETS:
    print('SKIP the EUC-JP / Shift_JIS runs (make charsets builds them)')


class Session:
    """one boot of MK09 with a fresh disk: /bin (sh, ls, jwp ...), /etc/rc.sh, and the given files in the root"""

    def __init__(self, level=1, kanji='utf8', args='', files=None):
        self.kanji = kanji
        build = BUILDS[kanji]
        self.dir = tempfile.mkdtemp(prefix='jwpt_')
        tree = os.path.join(self.dir, 'tree')
        os.makedirs(os.path.join(tree, 'bin'))
        os.makedirs(os.path.join(tree, 'etc'))
        for f in os.listdir(os.path.join(build, 'tree', 'bin')):
            if f in ('sh.mx', 'ls.mx', 'cat.mx', 'jwp.mx', 'echo.mx'):
                shutil.copy(os.path.join(build, 'tree', 'bin', f), os.path.join(tree, 'bin', f))
        shutil.copy(os.path.join(build, 'tree', 'etc', 'rc.sh'), os.path.join(tree, 'etc', 'rc.sh'))
        for name, data in (files or {}).items():
            with open(os.path.join(tree, name), 'wb') as f:
                f.write(data if isinstance(data, bytes) else data.encode('utf-8'))
        self.img = os.path.join(self.dir, 'disk.img')
        subprocess.run([sys.executable, '-X', 'utf8', MKDISK, '-o', self.img, '--boot', os.path.join(build, 'boot.img'),
                        '--size-mb', '16' if any(len(v) > 1000000 for v in (files or {}).values()) else '4', '--spc', '4' if any(len(v) > 1000000 for v in (files or {}).values()) else '1', tree], check=True, stdout=subprocess.DEVNULL)
        self.rom = os.path.join(build, 'bootrom.bin')
        self.args = args

    def run(self, keys, tail=(), timeout=300, sync=False, wait=None):
        """keys: list of strings sent to jwp one after the other (with a short pause).
        tail: extra script lines appended after jwp ended."""
        script = os.path.join(self.dir, 'script.txt')
        with open(script, 'w', encoding='utf-8') as f:
            f.write('timeout 60000\nexpect "> "\n')
            f.write('send "jwp %s\\r"\n' % self.args)
            f.write('expect "桁 "\nsleep 400\n')
            for k in keys:
                f.write('send "%s"\nsleep 250\n' % k)
            if wait:      # wait for a message of jwp
                f.write('expect "%s"\nsleep 150\n' % wait)
            elif sync:      # a search that fails: the message is drawn last, wait for it
                f.write('send "\\x12"\nsleep 250\nsend "ZQZQ\\r"\nexpect "見つかりません"\nsleep 150\n')
            else:
                f.write('sleep 4000\n')
            for t in tail:
                f.write(t + '\n')
            f.write('quit\n')
        cmd = [SIM, '-max', '-rom', self.rom, '-0', self.img, '-kanji', self.kanji, '-script', script]
        env = dict(os.environ, LC_ALL='C')
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, env=env)
        self.rc = p.returncode
        self.raw = p.stdout
        text = p.stdout.decode('utf-8', 'replace')
        self.text = text
        i = text.find('\x1b[2J')
        self.scr = Screen()
        self.scr.feed(text[i:] if i >= 0 else '')
        if VERBOSE:
            print('\n'.join('|' + l for l in self.scr.lines()))
        return self

    def host(self, name):
        import fatlib
        data = fatlib.Volume(self.img).read('/' + name)
        if data is None:
            raise FileNotFoundError(name)
        return data

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


import random


def fuzz_case(seed, npara=7, maxlen=50):
    """random wrapped text (short paragraphs) and a random key sequence of single key presses"""
    rnd = random.Random(seed)
    pieces = ['あ', 'い', '漢字', '日本語', '。', '、', '「', '」', 'ー', 'ab', 'word', ' ', 'XYZ', '！']
    keys1 = ['あ', 'い', '漢', '。', '、', '「', '」', 'ー', 'a', 'w', ' ', '！', '\\r']
    paras = []
    for _ in range(npara):
        paras.append(''.join(rnd.choice(pieces) for _ in range(rnd.randint(8, maxlen))))
    text = '\r'.join(paras) + '\r'
    keys = []
    for _ in range(100):
        op = rnd.random()
        if op < 0.35:
            keys.append(rnd.choice(keys1))
        elif op < 0.48:
            keys.append('\\x08')
        elif op < 0.55:
            keys.append('\\x04')
        elif op < 0.58:
            keys.append('\\x0b')
        else:
            keys.append(rnd.choice(['\\x1b[A', '\\x1b[B', '\\x1b[C', '\\x1b[D', '\\x1b[B', '\\x1b[B', '\\x16']))
    keys += ['\\x1b[B'] * 3 + ['\\x1b[A'] * 2 + ['\\x1b[C']
    return text, keys


def esc(s):
    """text -> script string literal (UTF-8 bytes stay literal)"""
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '\\r').replace('\n', '\\n')


if not os.path.exists(os.path.join(BUILDS['utf8'], 'tree', 'bin', 'jwp.mx')) or not os.path.exists(SIM):
    print('SKIP: build mk09 first (make in mk09/) and sim09')
    sys.exit(0)

for level in (1,):
    tag = 'MK09'
    # --- start, display, edit, save, quit
    s = Session(level, files={'t.txt': 'hello world\r'}, args='t.txt')
    s.run(['ABC', '\\x13'])
    ls = s.scr.lines()
    check(ls[0].startswith('ABChello world'), '%s jwp loads, inserts text and shows it' % tag, '\n'.join(ls[:3]))
    check('桁 4' in ls[22] and 'UTF-8' in ls[22] and 'CR' in ls[22], '%s status line (column, encoding, line end)' % tag, ls[22])
    check('保存しました' in ls[23], '%s ^S saves (message in Japanese)' % tag, ls[23])
    check(s.host('t.txt') == b'ABChello world\r', '%s the file keeps the OS-9 CR line end' % tag, repr(s.host('t.txt')))
    s.close()


    # --- Japanese editing: multibyte backspace / cursor keys / delete
    s = Session(level, args='j.txt')
    s.run(['日本語ワープロ', '\\x08\\x08', '\\x1b[D', '\\x1b[D', 'の', '\\x04', '\\x13'])
    ls = s.scr.lines()
    # 日本語ワープロ -> backspace twice -> 日本語ワー -> left x2 -> cursor before ワ -> の -> 日本語のワー -> ^D deletes ワ
    check(ls[0] == '日本語のー', '%s multibyte backspace, cursor keys, insert and delete work per character' % tag, repr(ls[0]))
    check(s.host('j.txt') == '日本語のー\n'.encode('utf-8'), '%s saved file contains the edited text' % tag, repr(s.host('j.txt')))
    check('桁 9' in ls[22], '%s column counts full width characters as two columns' % tag, ls[22])
    s.close()

    # --- soft wrap at 80 columns, wide characters
    text = ''.join(chr(0x3042 + (i % 40)) for i in range(50))     # 50 hiragana = 100 columns
    s = Session(level, files={'w.txt': text + '\r'}, args='w.txt')
    s.run([])
    ls = s.scr.lines()
    check(ls[0] == text[:40] and ls[1] == text[40:], '%s a 100 column paragraph wraps after 40 full width characters' % tag, repr(ls[:2]))
    s.close()

    # --- kinsoku: no punctuation at the start of a line, no opening bracket at the end
    t1 = 'あ' * 39 + '。' + 'い' * 5            # the 。 would be the 40th char -> fits
    t2 = 'あ' * 40 + '。' + 'い' * 5            # 。 is the first char of line 2 -> pushed: last あ moves down
    t3 = 'あ' * 39 + '「' + 'い' * 5            # 「 would end line 1 -> goes to line 2
    s = Session(level, files={'k1.txt': t1 + '\r', 'k2.txt': t2 + '\r', 'k3.txt': t3 + '\r'}, args='k2.txt')
    s.run([])
    ls = s.scr.lines()
    check(ls[0] == 'あ' * 39 and ls[1] == 'あ。' + 'い' * 5, '%s kinsoku: a closing mark is never put at the start of a line' % tag, repr(ls[:2]))
    s.close()
    s = Session(level, files={'k3.txt': t3 + '\r'}, args='k3.txt')
    s.run([])
    ls = s.scr.lines()
    check(ls[0] == 'あ' * 39 and ls[1] == '「' + 'い' * 5, '%s kinsoku: an opening bracket is never left at the end of a line' % tag, repr(ls[:2]))
    s.close()

    # --- latin words wrap at spaces
    words = ' '.join(['word%02d' % i for i in range(16)])      # 16*7-1 = 111 chars
    s = Session(level, files={'e.txt': words + '\r'}, args='e.txt')
    s.run([])
    ls = s.scr.lines()
    check(ls[0] == ' '.join(['word%02d' % i for i in range(11)]) and ls[1].startswith('word11'),
          '%s latin text wraps at spaces' % tag, repr(ls[:2]))
    s.close()

    # --- line end conventions: LF and CRLF files are detected and written back unchanged
    s = Session(level, files={'lf.txt': 'one\ntwo\n', 'crlf.txt': 'one\r\ntwo\r\n'}, args='lf.txt')
    s.run(['X', '\\x13', '\\x0f', 'crlf.txt\\r', 'Y', '\\x13'])
    ls = s.scr.lines()
    check(s.host('lf.txt') == b'Xone\ntwo\n' and s.host('crlf.txt') == b'Yone\r\ntwo\r\n',
          '%s LF / CRLF files keep their line end convention (^O opens another file)' % tag,
          repr((s.host('lf.txt'), s.host('crlf.txt'))))
    check('CRLF' in ls[22], '%s the status line shows the line end convention' % tag, ls[22])
    s.close()

    # --- search, kill and yank, new file
    s = Session(level, files={'s.txt': '東京\r大阪\r名古屋\r'}, args='s.txt')
    s.run(['\\x12', '大阪\\r', 'X', '\\x0b', '\\x0e', '\\x19', '\\x13'])
    ls = s.scr.lines()
    # ^R finds 大阪, X is typed in front, ^K kills it, ^N goes down to column 1 of the next line (before 名), ^Y yanks it back
    check(s.host('s.txt') == '東京\rX\r大阪名古屋\r'.encode('utf-8'),
          '%s search (^R), kill line (^K) and yank (^Y)' % tag, repr(s.host('s.txt').decode('utf-8', 'replace')))
    s.close()
    s = Session(level, args='new.txt')
    s.run(['テスト\\r', '\\x13'])
    check(s.host('new.txt') == 'テスト\n'.encode('utf-8'),
          '%s a new file is created by ^S' % tag, repr(s.host('new.txt')))
    check('新しいファイルです' in s.text, '%s new file message' % tag)
    s.close()

    # --- formatted export (hard wrapped copy, width from -e)
    text = ''.join(chr(0x3042 + (i % 40)) for i in range(30)) + '。' + '漢' * 6
    s = Session(level, files={'x.txt': text + '\r'}, args='-e 20 x.txt')
    s.run(['\\x18', 'out.txt\\r'])
    out = s.host('out.txt').decode('utf-8')
    rows_ = out.split('\r')
    check(all(len(r_) <= 10 for r_ in rows_) and ''.join(rows_) == text and not any(r_.startswith('。') for r_ in rows_),
          '%s ^X exports a hard wrapped copy (width 20 columns, kinsoku applied)' % tag, repr(out))
    s.close()


for level in (1,):
    tag = 'MK09'
    # --- scrolling: 60 lines, page down shows later lines, ^V ^U and the bottom of the file
    lines = ['行%02d' % i for i in range(60)]
    s = Session(level, files={'big.txt': '\r'.join(lines) + '\r'}, args='big.txt')
    s.run(['\\x1b[6~'])
    ls = s.scr.lines()
    check(ls[0] == '行00' and ls[21] == '行21', '%s the screen shows 22 text lines' % tag, repr(ls[:2] + ls[20:22]))
    s.close()
    s = Session(level, files={'big.txt': '\r'.join(lines) + '\r'}, args='big.txt')
    s.run(['\\x16', '\\x16', '\\x16', '\\x16'])
    ls = s.scr.lines()
    check('行' in ls[0] and ls[0] != '行00' and '位置' in ls[22], '%s page down scrolls the window' % tag, repr(ls[0]))
    s.close()
    s = Session(level, files={'big.txt': '\r'.join(lines) + '\r'}, args='big.txt')
    s.run(['\\x1b>'])
    ls = s.scr.lines()
    check(ls[21].strip() == '' and '行59' in ls[20], '%s ESC > goes to the end of the text' % tag, repr(ls[18:22]))
    s.close()

    # --- vertical cursor movement keeps the display column across wide / narrow characters
    s = Session(level, files={'u.txt': 'abcdef\r日本語です\rxyz\r'}, args='u.txt')
    s.run(['\\x1b[B', '\\x1b[C', '\\x1b[C', '\\x1b[A', 'Q', '\\x1b[B', '\\x1b[B', 'R', '\\x13'])
    # down (col 0) -> right x2 on 日本語 (col 4) -> up: col 4 of abcdef -> Q before 'e'; down,down: col 5 of line 3 -> clamps to the end
    got = s.host('u.txt').decode('utf-8')
    check(got == 'abcdQef\r日本語です\rxyzR\r', '%s up/down use the goal column (wide characters count two columns)' % tag, repr(got))
    s.close()

    # --- ^W save as, ^Q with unsaved changes asks
    s = Session(level, files={'q.txt': 'abc\r'}, args='q.txt')
    s.run(['X', '\\x11'])
    ls = s.scr.lines()
    check('保存せずに終了しますか' in ls[23], '%s ^Q on a modified text asks for confirmation' % tag, ls[23])
    s.close()
    s = Session(level, files={'q.txt': 'abc\r'}, args='q.txt')
    s.run(['X', '\\x11', 'y'], tail=['send "ls\\r"', 'expect "Q.TXT"'])
    check('Q.TXT' in s.text.split('保存せずに終了')[-1], '%s answering y quits to the shell (reloaded by PM) without saving' % tag, s.text[-200:])
    check(s.host('q.txt') == b'abc\r', '%s the unsaved change is not written' % tag)
    s.close()
    s = Session(level, files={'q.txt': 'abc\r'}, args='q.txt')
    s.run(['X', '\\x17', '\\x08' * 5 + 'copy.txt\\r'])
    check(s.host('copy.txt') == b'Xabc\r' and s.host('q.txt') == b'abc\r', '%s ^W saves under another name' % tag)
    check('copy.txt' in s.scr.line(22), '%s the status line follows the new name' % tag, s.scr.line(22))
    s.close()

    # --- a file bigger than the buffer is truncated, reported, and ^S refuses to overwrite it
    big = ('0123456789' * 8 + '\r') * 300       # 24300 bytes
    s = Session(level, files={'huge.txt': big}, args='huge.txt')
    s.run(['\\x13'], wait='大きすぎます')
    check('大きすぎます' in s.scr.line(23), '%s a file that does not fit is reported and ^S refuses to overwrite it' % tag, s.scr.line(23))
    m_ = re.search(r'0/(\d+)', s.scr.line(22))
    check(m_ and int(m_.group(1)) >= 2500, '%s the text buffer is %s bytes' % (tag, m_.group(1) if m_ else '?'), s.scr.line(22))
    check(s.host('huge.txt') == big.encode(), '%s the file is intact' % tag)
    s.close()

# --- incremental layout / redraw: random edits on wrapped text, compared with a full redraw (^L)
for level, seeds in ((1, range(6)), (1, range(100, 106))):
    bad = 0
    for seed in seeds:
        text, keys = fuzz_case(seed, 7 if seed < 100 else 16, 50 if seed < 100 else 90)
        a = Session(level, files={'f.txt': text}, args='f.txt').run(keys, sync=True)
        b = Session(level, files={'f.txt': text}, args='f.txt').run(keys + ['\\x0c'], sync=True)
        la, lb = a.scr.lines()[:22], b.scr.lines()[:22]
        if la != lb or (a.scr.r, a.scr.c) != (b.scr.r, b.scr.c):
            bad += 1
            if la == lb:
                print('     seed %d cursor %r / %r' % (seed, (a.scr.r, a.scr.c), (b.scr.r, b.scr.c)))
            for i_, (x_, y_) in enumerate(zip(la, lb)):
                if x_ != y_:
                    print('     seed %d row %d: incremental %r / full %r' % (seed, i_, x_, y_))
                    break
        a.close()
        b.close()
    check(bad == 0, 'MK09 incremental redraw (scrolling, insert/delete lines) equals a full redraw after random edits (%d runs%s)' % (len(seeds), ', long text' if seeds[0] >= 100 else ''))


# =============================================================== SKK input method
import importlib.util
_spec = importlib.util.spec_from_file_location('mkskkdic', os.path.join(ROOT, 'tools', 'mkskkdic.py'))
mkskkdic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mkskkdic)

SKK_ENTRIES = [
    ('かk', ['書', '掛', '欠']), ('おくr', ['送', '贈']), ('たべr', ['食べ']),
    ('かんじ', ['漢字', '感じ', '幹事']), ('にほん', ['日本', '二本']), ('にほんご', ['日本語']),
    ('せかい', ['世界']), ('こんにちは', ['今日は']), ('あい', ['愛', '哀', '相', '藍', '合', '会', '間', '逢', '娃', '亜']),
    ('わーぷろ', ['ワープロ']), ('しゃしん', ['写真']), ('きって', ['切手']), ('とうきょう', ['東京']),
    ('abc', ['ＡＢＣ']),
]


def skk_dic(enc, entries=SKK_ENTRIES):
    return mkskkdic.build(entries, enc)[0]


def skk_session(level, kj='utf8', entries=SKK_ENTRIES, user=None, files=None, args='memo.txt'):
    codec = {'utf8': 'utf-8', 'euc': 'euc_jp', 'sjis': 'cp932'}[kj]
    f = {'skk.dic': skk_dic(kj, entries)}
    if user is not None:
        f['skku.dic'] = user.encode(codec)
    f.update(files or {})
    return Session(level, kj, files=f, args='-d /skk.dic -u /skku.dic ' + args)


def result(s, name='memo.txt', codec='utf-8'):
    """text of the saved file (CR -> LF)"""
    return s.host(name).decode(codec).replace('\r', '\n')


for level in (1,):
    tag = 'MK09'
    # --- romaji -> kana (hiragana, katakana, small tsu, n, long vowel, punctuation)
    s = skk_session(level)
    s.run(['\\x0a', 'aiueo kya kitte kanji onna ka-pu', ',.[]', 'q', 'kana', 'q', 'sha', '\\x13'], sync=False, wait='保存しました')
    check(result(s) == 'あいうえお きゃ きって かんじ おんな かーぷ、。「」カナしゃ\n', '%s romaji input: hiragana, katakana, っ ん ー and punctuation' % tag, repr(result(s)))
    check('かな' in s.scr.line(22), '%s the status line shows the input mode' % tag, s.scr.line(22))
    s.close()

    # --- conversion: candidates, next / previous, cancel, accept with Enter or with another key
    s = skk_session(level)
    s.run(['\\x0a', 'Kanji', ' ', '\\r', '\\r', 'Kanji', ' ', ' ', '\\r', '\\r', 'Nihon', ' ', ' ', 'x', '\\r', '\\r',
           'Sekai', ' ', '\\x07', 'ji', '\\r', '\\r', 'Nihongo', ' ', 'wo', '\\x13'], wait='保存しました')
    check(result(s) == '漢字\n感じ\n日本\nせかいじ\n日本語を\n', '%s SKK conversion: Space, Space+x, ^G, and the next key accepts the candidate' % tag, repr(result(s)))
    s.close()

    # --- okurigana
    s = skk_session(level)
    s.run(['\\x0a', 'KaKu', '\\r', '\\r', 'OkuRu', '\\r', '\\r', 'TabeRu', '\\r', '\\r', '\\x13'], wait='保存しました')
    check(result(s) == '書く\n送る\n食べる\n', '%s okurigana: KaKu -> 書く, OkuRu -> 送る, TabeRu -> 食べる' % tag, repr(result(s)))
    s.close()

    # --- the 5th candidate on: a list with the keys a s d f j k l
    s = skk_session(level)
    s.run(['\\x0a', 'Ai', ' ', ' ', ' ', ' ', ' '])
    check('a:合' in s.scr.line(23) and 's:会' in s.scr.line(23), '%s the 5th candidate on is listed with selection keys' % tag, s.scr.line(23))
    s.close()
    s = skk_session(level)
    s.run(['\\x0a', 'Ai', ' ', ' ', ' ', ' ', ' ', 'k', '\\x13'], wait='保存しました')
    check(result(s) == '亜\n', '%s a candidate is selected with its key (k = the 6th of the list)' % tag, repr(result(s)))
    s.close()

    # --- ▽ / ▼ markers, BS in a reading, abbreviation, katakana with q
    s = skk_session(level)
    s.run(['\\x0a', 'Kanji'])
    check(s.scr.line(0) == '▽かんじ', '%s the reading is shown after the ▽ marker' % tag, s.scr.line(0))
    s.close()
    s = skk_session(level)
    s.run(['\\x0a', 'Kanji', ' '])
    check(s.scr.line(0) == '▼漢字', '%s the candidate is shown after the ▼ marker' % tag, s.scr.line(0))
    s.close()
    s = skk_session(level)
    s.run(['\\x0a', 'Kanji', '\\x08', '\\x08', '\\r', '\\x13'], wait='保存しました')
    check(result(s) == 'か\n', '%s BS in a reading deletes kana, Enter keeps the reading' % tag, repr(result(s)))
    s.close()
    s = skk_session(level)
    s.run(['\\x0a', '/abc', ' ', '\\r', 'Wa-puro', 'q', ' ', '\\r', '\\x13'], wait='保存しました')
    check(result(s).startswith('ＡＢＣ'), '%s abbreviation mode: /abc converts the ASCII reading' % tag, repr(result(s)))
    s.close()

    # --- registration of a word that is not in the dictionary, and the user dictionary (read first, appended)
    s = skk_session(level)
    s.run(['\\x0a', 'Hoge', ' ', 'ほげ！\\r', '\\x13'], wait='保存しました')
    check(result(s) == 'ほげ！\n', '%s a word without candidates is registered (prompt) and inserted' % tag, repr(result(s)))
    check(s.host('skku.dic') == 'ほげ /ほげ！/\r'.encode('utf-8'), '%s the registered word is in the user dictionary' % tag, repr(s.host('skku.dic')))
    s.close()
    s = skk_session(level, user='ほげ /ほげ！/\r')
    s.run(['\\x0a', 'Hoge', ' ', '\\r', '\\x13'], wait='保存しました')
    check(result(s) == 'ほげ！\n', '%s the user dictionary is consulted' % tag, repr(result(s)))
    s.close()
    s = skk_session(level)
    s.run(['\\x0a', 'Kanji', ' ', ' ', '\\r', '\\r', 'Kanji', ' ', '\\r', '\\x13'], wait='保存しました')
    check(s.host('skku.dic') == 'かんじ /感じ/\r'.encode('utf-8'), '%s choosing another candidate is learned' % tag, repr(s.host('skku.dic')))
    check(result(s) == '感じ\n感じ\n', '%s the learned candidate comes first next time' % tag, repr(result(s)))
    s.close()


# --- SKK in the other console encodings (dictionary and text in the guest character set)
for kj, codec in CHARSETS:
    s = skk_session(1, kj)
    s.run(['\\x0a', 'aiueo ', 'q', 'kana', 'q', '\\r', 'Kanji', ' ', ' ', '\\r', '\\r', 'KaKu', '\\r', '\\r', 'Hoge', ' ', 'ほげ\\r', '\\x13'], wait='保存しました')
    check(result(s, codec=codec) == 'あいうえお カナ\n感じ\n書く\nほげ\n', '[%s] SKK: romaji, katakana, conversion with learning, okurigana, registration' % kj, repr(result(s, codec=codec)))
    check(s.host('skku.dic') == ('かんじ /感じ/\rほげ /ほげ/\r').encode(codec), '[%s] the user dictionary is written in the console character set' % kj, repr(s.host('skku.dic')))
    s.close()

# --- no dictionary: the word is asked for; the markers and the 'q' key in a reading
s = Session(1, args='-d /none.dic -u /skku.dic memo.txt')
s.run(['\\x0a', 'Kanji', ' '])
check(s.scr.line(23).startswith('辞書登録 かんじ:'), 'MK09 without a dictionary a registration prompt follows the conversion key', s.scr.line(23))
s.close()
s = skk_session(1)
s.run(['\\x0a', 'Katakana', 'q', ' ', '\\x13'], wait='保存しました')
check(result(s).startswith('カタカナ'), 'MK09 q in a reading turns it into katakana', repr(result(s)))
s.close()

# --- the dictionary search on a big dictionary (needs SKK-JISYO.L of the skkdic package)
SKKL = '/usr/share/skk/SKK-JISYO.L'
if os.path.exists(SKKL):
    ents = mkskkdic.read_skk(SKKL)
    HIRA = {'あ': 'a', 'い': 'i', 'う': 'u', 'え': 'e', 'お': 'o', 'や': 'ya', 'ゆ': 'yu', 'よ': 'yo', 'わ': 'wa', 'を': 'wo', 'ん': "n'"}
    for c, kana in zip('ksthmrgzdbp', ['かきくけこ', 'さしすせそ', 'たちつてと', 'はひふへほ', 'まみむめも', 'らりるれろ', 'がぎぐげご', 'ざじずぜぞ', 'だぢづでど', 'ばびぶべぼ', 'ぱぴぷぺぽ']):
        for v, k in zip('aiueo', kana):
            HIRA[k] = c + v
    for v, k in zip('aiueo', 'なにぬねの'):
        HIRA[k] = 'n' + v

    def romaji(key):
        out = ''
        for ch in key:
            if ch not in HIRA:
                return None
            out += HIRA[ch]
        return out

    rnd = random.Random(7)
    pick = []
    for key, cands in ents:
        r = romaji(key)
        if r and key[0] not in 'んを' and 'ゐ' not in key and not r.endswith("n'") and len(key) >= 3:
            pick.append((key, r, cands[0]))
    rnd.shuffle(pick)
    pick = pick[:24]
    # the first candidate of every picked reading must come out of the binary search on the 4.5 MB file
    keys, expect = ['\\x0a'], ''
    for key, r, cand in pick:
        keys += [r[0].upper() + r[1:], ' ', '\\r', '\\r']
        expect += cand + '\n'
    for level in (1,):
        s = Session(level, files={'skk.dic': mkskkdic.build(ents, 'utf8')[0]}, args='-d /skk.dic -u /skku.dic memo.txt')
        s.run(keys + ['\\x13'], wait='保存しました', timeout=600)
        check(result(s) == expect, 'MK09 binary search in the full SKK-JISYO.L (%d random readings)' % len(pick),
              '\n'.join('%s -> %s' % (a, b) for a, b in zip(result(s).split('\n'), expect.split('\n')) if a != b)[:600])
        s.close()
else:
    print('SKIP big dictionary test (SKK-JISYO.L is not installed)')

# --- the same random edit test in the EUC-JP and Shift_JIS console modes
for kj, codec in CHARSETS:
    bad = 0
    for seed in range(200, 204):
        text, keys = fuzz_case(seed)
        data = text.encode(codec)
        a = Session(1, kj, files={'f.txt': data}, args='f.txt').run(keys, sync=True)
        b = Session(1, kj, files={'f.txt': data}, args='f.txt').run(keys + ['\\x0c'], sync=True)
        if a.scr.lines()[:22] != b.scr.lines()[:22] or (a.scr.r, a.scr.c) != (b.scr.r, b.scr.c):
            bad += 1
        a.close()
        b.close()
    check(bad == 0, '[%s] incremental redraw equals a full redraw after random edits (4 runs)' % kj)

# --- random SKK input: the screen after the incremental redraws equals a full redraw (and no layout bug)
def skk_fuzz_keys(seed):
    rnd = random.Random(seed)
    pool = ['a', 'i', 'u', 'e', 'o', 'k', 'n', 's', 't', 'K', 'N', 'S', 'T', 'A', 'I', 'H', ' ', ' ', ' ', '\\r', 'x', '\\x08', '\\x07',
            'q', '\\x1b[D', '\\x1b[A', 'ka', 'Kanji', 'KaKu', 'Ai', 'Nihon', ',', '.', 'a', 'b', 'w', 'ー']
    keys = ['\\x0a']
    for _ in range(70):
        keys.append(rnd.choice(pool))
    return keys + ['\\x07'] * 3


for level, seeds in ((1, range(300, 304)),):
    bad = 0
    for seed in seeds:
        keys = skk_fuzz_keys(seed)
        text = '\\r'.join('あいうえお かきくけこ %d' % i for i in range(6)) + '\\r'
        a = skk_session(level, files={'memo.txt': text}).run(keys, sync=True)
        b = skk_session(level, files={'memo.txt': text}).run(keys + ['\\x0c'], sync=True)
        if a.scr.lines()[:22] != b.scr.lines()[:22] or (a.scr.r, a.scr.c) != (b.scr.r, b.scr.c) or 'LAYOUT-BUG' in a.text:
            bad += 1
            print('     seed %d' % seed, [x for x in a.scr.lines()[:6]])
        a.close()
        b.close()
    check(bad == 0, 'MK09 random SKK input: incremental redraw equals a full redraw (%d runs)' % len(seeds))

# --- the other Japanese encodings (console character set of the guest)
for kj, codec in CHARSETS:
    s = Session(1, kj, args='j.txt')
    s.run(['日本語ワープロ', '\\x08', '\\x1b[D', 'の', '\\x13'])
    ls = s.scr.lines()
    want = '日本語ワーのプ'.encode(codec)
    check(s.host('j.txt') == want + b'\n', '[%s] multibyte editing and saving in the guest encoding' % kj, repr(s.host('j.txt')))
    check(ls[0] == '日本語ワーのプ', '[%s] the screen shows the text' % kj, repr(ls[0]))
    check('保存しました' in ls[23], '[%s] messages are in the console encoding' % kj, ls[23])
    s.close()
    text = 'あ' * 40 + '。' + 'い' * 5
    s = Session(1, kj, files={'k.txt': (text + '\r').encode(codec)}, args='k.txt')
    s.run([])
    ls = s.scr.lines()
    check(ls[0] == 'あ' * 39 and ls[1] == 'あ。' + 'い' * 5, '[%s] kinsoku works in the encoding' % kj, repr(ls[:2]))
    s.close()

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
