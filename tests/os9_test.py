#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""System tests: boot OS-9 Level 1 / Level 2 on the simulator and exercise the
disks, the virtual disk, the clock and Japanese text handling.

Every test drives the console through the simulator's expect/send script
engine (-script) and checks the console output.  Run:  tests/os9_test.py [-v]
"""
import os, re, subprocess, sys, tempfile, shutil, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'sim09')
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


class Session:
    """one simulator run in a scratch directory"""

    def __init__(self, level, kanji='utf8', rom=None, extra=()):
        self.level = level
        self.kanji = kanji
        self.dir = tempfile.mkdtemp(prefix='os9t_')
        for d in ('OS9.dsk', 'WORK.dsk'):
            shutil.copy(os.path.join(ROOT, 'os9', d), self.dir)
        self.v0 = os.path.join(self.dir, 'v0')
        os.mkdir(self.v0)
        shutil.copytree(os.path.join(ROOT, 'os9', 'level%d' % level, 'cmds'), os.path.join(self.v0, 'cmds'))
        suffix = '' if kanji == 'utf8' else '_' + kanji
        self.rom = rom or os.path.join(ROOT, 'os9', 'os9v%d%s.rom' % (level, suffix))
        self.extra = list(extra)

    def run(self, steps, timeout=120):
        script = os.path.join(self.dir, 'script.txt')
        with open(script, 'w', encoding='utf-8') as f:
            f.write('timeout 30000\nexpect "OS9:"\nsend "chx /v0/cmds\\r"\nexpect "OS9:"\n')
            for st in steps:
                f.write(st + '\n')
            f.write('quit\n')
        cmd = [SIM] + (['-2'] if self.level == 2 else []) + [
            '-max', '-rom', self.rom, '-0', os.path.join(self.dir, 'OS9.dsk'),
            '-1', os.path.join(self.dir, 'WORK.dsk'), '-v', self.v0,
            '-kanji', self.kanji, '-script', script] + self.extra
        env = dict(os.environ, LC_ALL='C')
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, env=env)
        self.rc = p.returncode
        self.out = p.stdout
        if VERBOSE:
            print(self.out.decode('utf-8', 'replace'))
        return self.out

    def text(self):
        return self.out.decode('utf-8', 'replace')

    def close(self):
        shutil.rmtree(self.dir, ignore_errors=True)


def cmds(*lines):
    r = []
    for l in lines:
        r.append('send "%s\\r"' % l)
        r.append('expect "OS9:"')
    return r


for level in (1, 2):
    tag = 'L%d' % level
    s = Session(level)
    s.run(cmds('mdir -e' if level == 2 else 'mdir', 'dir /d0', 'free /d0', 'dir /v0', 'devs'))
    t = s.text()
    check(s.rc == 0 and 'NitrOS-9/6809 Level %d' % level in t, '%s boots to the shell prompt' % tag, t[-400:])
    check('SCF' in t and 'NPty' in t and 'N1' in t, '%s module directory has SCF and the OS-9 Net modules' % tag)
    check('OS9Boot' in t, '%s /d0 (raw RBF disk image) can be read' % tag)
    check('Free sectors' in t or 'free sectors' in t.lower(), '%s free reports the sector bitmap' % tag)
    check('cmds' in t, '%s /v0 (virtual RBF) lists the host directory' % tag)
    check('TERM' in t and 'V0' in t, '%s devs lists the device table' % tag)
    s.close()

    # --- file operations on /d1 (raw image) and /v0 (host dir) ---
    s = Session(level)
    s.run(cmds('chd /v0', 'makdir work', 'chd work', 'copy /d0/startup t1.txt', 'list t1.txt', 'dir',
               'copy t1.txt t2.txt', 'dir', 'dir /d1', 'del t1.txt', 'del t2.txt', 'dir', 'chd /v0', 'deldir work', 'dir'))
    t = s.text()
    check(os.path.isdir(os.path.join(s.v0, 'work')) is False, '%s makdir/deldir on /v0 reach the host file system' % tag)
    check(t.count('t1.txt') >= 3 and 't2.txt' in t, '%s copy and dir on /v0' % tag, t[-600:])
    check('printerr' in t and 'DEFS' in t, '%s files of the raw disk images are readable (/d0 file, /d1 directory)' % tag)
    s.close()

    # --- case insensitive names, lower/upper case ---
    s = Session(level)
    s.run(cmds('chd /V0/CMDS', 'DIR -E', 'chd /v0'))
    check(s.rc == 0 and 'Directory of' in s.text(), '%s OS-9 names are case insensitive on /v0' % tag)
    s.close()

    # --- real time clock ---
    s = Session(level)
    s.run(cmds('date'))
    year = str(datetime.date.today().year)
    check(year in s.text(), '%s clock/RTC shows the host date (%s)' % (tag, year))
    s.close()

    # --- Japanese: UTF-8 round trip, editing, file names ---
    for kj in ('utf8', 'euc', 'sjis'):
        s = Session(level, kj)
        host = os.path.join(s.v0, '日本語.txt')
        with open(host, 'w', encoding='utf-8') as f:
            f.write('こんにちは、世界\nsecond line\n')
        steps = ['send "chd /v0\\r"', 'expect "OS9:"', 'send "dir\\r"', 'expect "OS9:"',
                 'send "copy 日本語.txt コピー.txt\\r"', 'expect "OS9:"',
                 'send "dir\\r"', 'expect "OS9:"', 'send "kmode\\r"', 'expect "OS9:"',
                 'send "echo あいう\\x08\\x08え\\r"', 'expect "OS9:"']
        s.extra = ['-kname']
        s.run(steps)
        t = s.text()
        names = [n for n in ('日本語.txt', 'コピー.txt') if n in t]
        check(len(names) == 2, '%s [%s] Japanese file names are listed and copied' % (tag, kj), t[-500:])
        check(os.path.exists(os.path.join(s.v0, 'コピー.txt')) and
              open(os.path.join(s.v0, 'コピー.txt'), 'rb').read() == open(host, 'rb').read(),
              '%s [%s] copy of a file with a Japanese name has identical contents' % (tag, kj))
        check({'utf8': 'UTF-8', 'euc': 'EUC-JP', 'sjis': 'Shift_JIS'}[kj] in t, '%s [%s] kmode reports the encoding' % (tag, kj))
        # backspace over two characters: the display column of each erased char is 2
        # (BSE,SP,BSE twice) -> 4 erase sequences, buffer ends up "あえ"
        check('あえ' in t.split('echo あいう')[-1], '%s [%s] multibyte backspace edits whole characters' % (tag, kj), t[-300:])
        check(t.count('\b \b') >= 4, '%s [%s] erase sequence echoed per display column' % (tag, kj))
        s.close()

    # --- timer / scheduler sanity: two processes make progress ---
    s = Session(level)
    s.run(cmds('procs', 'sleep 1', 'echo done'))
    check('done' in s.text(), '%s sleep/scheduler works' % tag)
    s.close()

# --- creating, reading and deleting files on the raw RBF images (PDisk must fill the drive table from LSN 0)
for level in (1, 2):
    s = Session(level)
    s.run(cmds('copy /d0/startup /d0/new1', 'copy /d0/startup /d1/new2', 'dir /d0', 'dir /d1',
               'list /d1/new2', 'makdir /d1/sub', 'copy /d0/new1 /d1/sub/new3', 'dir /d1/sub', 'del /d0/new1', 'dir /d0'))
    t = s.text()
    check('new1' in t and 'new2' in t and 'new3' in t and t.count('printerr /d0/sys/errmsg') >= 1,
          'L%d files can be created on /d0 and /d1 (RBF write path)' % level, t[-500:])
    check(t.rsplit('Directory of /d0', 1)[-1].count('new1') == 0, 'L%d del removes a file from the raw disk image' % level)
    img = open(os.path.join(s.dir, 'WORK.dsk'), 'rb').read()
    check(b'new2' in img[:60000] or b'new\xb2' in img, 'L%d the new file name is on the disk image file' % level)
    s.close()

# --- shutdown command powers the simulator off cleanly
for level in (1, 2):
    s = Session(level)
    s.run(['send "shutdown\\r"', 'expect "NEVER-APPEARS" 5000'])
    check(s.rc == 0 and 'System halted.' in s.text() and 'timeout' not in s.text(),
          'L%d shutdown ends the simulator with exit code 0' % level)
    s.close()

# --- regression: a09 mis-assembled help / error (wrong module size and entry) and `help` froze the system
for level in (1, 2):
    bad = []
    d = os.path.join(ROOT, 'os9', 'level%d' % level, 'cmds')
    for f in sorted(os.listdir(d)):
        p = os.path.join(d, f)
        if f.endswith(('.lst', '.asm', '.pl')) or f in ('Makefile', 'defsfile') or not os.path.isfile(p):
            continue
        b = open(p, 'rb').read()
        if b[:2] == b'\x87\xcd' and int.from_bytes(b[2:4], 'big') != len(b):
            bad.append(f)
    check(not bad, 'L%d all commands have a module size equal to the file size' % level, str(bad))
    s = Session(level)
    s.run(cmds('help', 'error 216', 'dir /d0'))
    check('OS9Boot' in s.text(), 'L%d help and error return to the prompt' % level)
    s.close()

# --- regression: the original Level 1 clock decremented bytes of the user's data page
# (DP was not 0 in the clock IRQ) -> garbled output depending on the timer phase
for level in (1, 2):
    bad = 0
    for ms in range(0, 300, 37):
        s = Session(level)
        s.run(['sleep %d' % ms] + cmds('dir /v0', 'dir -e /v0/cmds'))
        t = s.text()
        if not re.search(r'Directory of /v0 ', t) or re.search(r'Direct[^o]ry|Directo[^r]y|2/26', t):
            bad += 1
        s.close()
    check(bad == 0, 'L%d clock IRQ does not corrupt user data (8 timer phases)' % level)

# --- the whole OS-9 Level 1 on the disk: the 256 byte boot ROM of FUZIX / MK09 loads the kernel (Boot, Init, IOMan, SCF, KrnP2, Krn:
# tools/mkkernel.py, LSN 2-17 and 720-735) into RAM and jumps to it; the same FPGA bitstream as FUZIX and MK09
s = Session(1, rom=os.path.join(ROOT, 'fuzix', 'images', 'bootrom.bin'))
s.run(['send "chx /d0/cmds\\r"', 'expect "OS9:"', 'send "mdir\\r"', 'expect "OS9:"', 'send "mfree\\r"', 'expect "OS9:"',
       'send "jwp /d0/t.txt\\r"', 'sleep 3000', 'send "\\021"', 'sleep 1000'])
t = s.text()
check('Tang Nano 9K' in t and 'Unknown Machine' not in t, 'L1 from the disk (boot ROM of FUZIX): the boot message names the machine, Tang Nano 9K', t[:300])
mods = t[t.find('Module Directory'):]
check(all(m in mods for m in ('Boot', 'Init', 'IOMan', 'SCF', 'KrnP2', 'Krn', 'RBF', 'Shell', 'SysGo')),
      'L1 from the disk: the kernel and its modules were loaded from the card', mods[:300])
check('ERROR #207' not in t and '/d0/t.txt' in t, 'L1 from the disk: jwp starts (175 free pages)', t[-300:])
s.close()

# --- the version of jwp: jwp -v (inside) and the edition of the module (outside: OS-9's ident)
s = Session(1)
s.run(cmds('jwp -v', 'ident /v0/cmds/jwp'))
t = s.text()
check('jwp 1.0 (c) 2026 YUKARI Semiconductor Devices' in t and re.search(r'Edition:\s+\$0A\s+#10', t) is not None,
      'jwp -v shows the version and the copyright, ident shows the edition 10', t[-400:])
s.close()

# --- Level 1 booted from the disk: os9v1boot.rom holds only the kernel, Init and the booter (os9/level1/boot.asm);
# IOMan, the file managers, the drivers, the shell ... come from the boot file OS9Boot on OS9.dsk (tools/mkboot.py)
s = Session(1, rom=os.path.join(ROOT, 'os9', 'os9v1boot.rom'))
s.run(cmds('mdir', 'dir /d0'))
t = s.text()
check(all(m in t for m in ('Boot', 'IOMan', 'RBF', 'SCF', 'Pdisk', 'Shell', 'SysGo')) and 'Directory of /d0' in t,
      'L1 boots from the boot file on OS9.dsk (the ROM has the kernel, the booter, IOMan and SCF)', t[-400:])
check(os.path.getsize(os.path.join(ROOT, 'os9', 'os9v1boot.rom')) == 8192, 'L1 the disk boot ROM is 8 KB')
# jwp needs about 174 free pages: with the modules of the boot file in RAM it did not start (ERROR #207) until IOMan and SCF moved into the ROM
s.run(['send "chx /d0/cmds\\r"', 'expect "OS9:"', 'send "jwp /d0/t.txt\\r"', 'sleep 3000', 'send "\\021"', 'sleep 1000'])
t = s.text()
check('ERROR #207' not in t and '/d0/t.txt' in t, 'L1 jwp starts with the disk boot ROM (enough free memory)', t[-300:])
s.close()

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
