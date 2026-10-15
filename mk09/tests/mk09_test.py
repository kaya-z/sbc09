#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of MK09 on the simulator (mk09/, docs: mk09/docs).

Builds nothing: run `make` in mk09/ first.  Boots build/mk09.img with the boot ROM, drives the
console with sim09 scripts and checks the shell, the programs, the file system (also with
fsck.fat on the volume afterwards) and the Japanese console handling.      Run:  tests/mk09_test.py [-v]
"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
SIM = os.path.join(ROOT, 'sim09')
sys.path.insert(0, HERE)
import fatlib

VERBOSE = '-v' in sys.argv
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-1500:])


BUILD = os.path.join(MK, os.environ.get('MK09_BUILD', 'build'))
ROM = os.path.join(BUILD, 'bootrom.bin')
IMG = os.path.join(BUILD, 'mk09.img')
if not (os.path.exists(ROM) and os.path.exists(IMG) and os.path.exists(SIM)):
    print('SKIP: run make in mk09/ (and in the top directory for sim09) first')
    sys.exit(0)

work = tempfile.mkdtemp(prefix='mk09t_')


def run(disk, steps, extra=(), timeout=600, rom=ROM):
    """steps: script lines; returns (output, return code)"""
    script = os.path.join(work, 's.txt')
    with open(script, 'w', encoding='utf-8') as f:
        f.write('timeout 120000\n')
        for s in steps:
            f.write(s + '\n')
    cmd = [SIM, '-max', '-rom', rom, '-0', disk] + list(extra) + ['-script', script]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                       env=dict(os.environ, LC_ALL='C'))
    out = p.stdout.decode('utf-8', 'replace').replace('\r', '')
    if VERBOSE:
        print(out)
    return out, p.returncode


def sh(*lines, prompt='> '):
    # (a command must not contain '> ' itself: the echo of the line would end the wait early)
    r = []
    for l in lines:
        r.append('send "%s\\r"' % l.replace('"', '\\"'))
        r.append('expect "%s"' % prompt)
    return r


BOOT = ['expect "type help"', 'expect "/> "']
P = '> '
OFF = ['send "shutdown\\r"', 'sleep 300']

disk = os.path.join(work, 'disk.img')
shutil.copy(IMG, disk)

# ---- boot, the shell and the programs
out, rc = run(disk, BOOT + sh('ls /', 'ls /bin', 'cat /etc/rc.sh', 'ver', 'date', 'ps', 'df', 'stty', 'nosuch', 'help', prompt=P) + OFF)
check('MK09 microkernel' in out, 'the system boots from the SD image and the shell starts', out[:300])
check(re.search(r'BIN/\s+DOC/\s+ETC/', out) is not None, 'ls / shows the directories of the volume', out[:600])
check(all(n in out for n in ('SH.MX', 'LS.MX', 'CAT.MX')), 'ls /bin lists the programs')
check('stty utf8' in out, 'cat reads /etc/rc.sh')
check(re.search(r'20\d\d-\d\d-\d\d \d\d:\d\d:\d\d', out) is not None, 'date shows the clock')
check(re.search(r'^\s*1\s+(receive|ready|sleep)', out, re.M) is not None and 'running' in out, 'ps lists the tasks (TTY and the running ps)', out)
check(re.search(r'cluster \d+ bytes, \d+ clusters, \d+ free', out) is not None, 'df reports the clusters')
check('utf8 cooked 80x24 echo' in out, 'stty shows the console mode')
check('nosuch: command not found' in out, 'an unknown command is reported')
check('name > file' in out, 'help prints the usage')
check(rc == 0, 'shutdown powers the simulator off (exit code 0)', 'rc=%d' % rc)

# ---- dhrystone (the checks of the final values are the ones of the benchmark; the time is the uptime)
out, rc = run(disk, BOOT + sh('dhry 200', prompt=P) + OFF)
check('Dhrystone checks OK' in out and 'WRONG' not in out, 'dhry: the final values of Dhrystone 2.1 are right', out[-700:])
m = re.search(r'Dhrystones per second: (\d+)', out)
check(m is not None and 100 < int(m.group(1)) < 1000, 'dhry: reports a plausible number of Dhrystones per second', m.group(0) if m else out[-300:])

# ---- grep, more, life
out, rc = run(disk, BOOT + sh(
    'echo apple pie >/g1.txt', 'echo banana split >>/g1.txt', 'echo Apple tart >>/g1.txt',
    'grep apple /g1.txt', 'grep -i -n apple /g1.txt', 'grep -v an /g1.txt', 'grep -c a /g1.txt',
    'grep ^ban /g1.txt', 'grep a.*t$ /g1.txt', 'grep zzz /g1.txt', 'grep apple /g1.txt /nofile',
    'grep -c マ.クロ /doc/guide.txt', prompt=P) + OFF)
check(re.search(r'grep apple /g1.txt\s+apple pie\s+/> ', out) is not None, 'grep finds a fixed string', out[-1500:])
check('1:apple pie' in out and '3:Apple tart' in out, 'grep -i -n ignores the case and numbers the lines')
check(re.search(r'grep -v an /g1.txt\s+apple pie\s+Apple tart\s+/> ', out) is not None, 'grep -v prints the lines that do not match')
check(re.search(r'grep -c a /g1.txt\s+3\s', out) is not None, 'grep -c counts the matching lines')
check(re.search(r'grep \^ban /g1.txt\s+banana split\s+/> ', out) is not None, 'grep ^ anchors at the start of the line')
check(re.search(r'banana split\s+Apple tart\s+/> grep zzz', out) is not None, 'grep with . * and $')
check(re.search(r'grep zzz /g1.txt\s+/> ', out) is not None, 'grep prints nothing when nothing matches')
check('/g1.txt:apple pie' in out and 'grep: /nofile: not found' in out, 'grep with several files prefixes the file name and reports a missing file')
check(re.search(r'マ.クロ /doc/guide.txt\s+[1-9]', out) is not None, 'grep: . matches a whole Japanese character')

steps = BOOT + ['send "stty rows=10\\r"', 'expect "%s"' % P, 'send "more /doc/guide.txt\\r"', 'expect "-- more --"', 'send " "',
                'expect "-- more --"', 'send "\\r"', 'expect "-- more --"', 'send "q"', 'expect "%s"' % P,
                'send "life -s 7 -d 0 -n 6\\r"', 'expect "gen 6"', 'sleep 300', 'send "stty\\r"', 'expect "echo"'] + OFF
out, rc = run(disk, steps)
check(out.count('-- more --') >= 3 and '\x1b[' not in out.split('more /doc/guide.txt')[1].split('life')[0], 'more stops after a screen, goes on with Space and Enter and ends with q', out[-800:])
check('gen 6  alive' in out and '\x1b[2J' in out, 'life runs 6 generations on the screen and shows the status line')
check('utf8 cooked' in out.split('gen 6')[1], 'life and more give the console its mode back')

# ---- the soft floating point and the BASIC
out, rc = run(disk, BOOT + sh('sfptest', prompt=P) + OFF)
check('sfp ok' in out, 'sfptest: the soft floating point (assembler) agrees with the float of the PC on 300 vectors', out[-600:])


def basic_cmds(*cmds):
    r = ['send "basic\\r"', 'expect "Ok"']
    for c in cmds:
        r.append('send "%s\\r"' % c.replace('"', '\\"'))
        r.append('expect "Ok"')
    r += ['send "bye\\r"', 'expect "%s"' % P]
    return r


out, rc = run(disk, BOOT + basic_cmds(
    'PRINT 1+2, 10/4, 2^10, -5, 0.0458*5, 1/3',
    'A=3:B=4:PRINT SQR(A*A+B*B), INT(-2.5), ABS(-7), SGN(-3)',
    'PRINT SIN(1), COS(1), ATN(1), EXP(1), LOG(10)',
    'PRINT "ab"+"cd"; LEN("hello"); ASC("A"); CHR$(66); LEFT$("abcdef",2); RIGHT$("abcdef",2); MID$("abcdef",3,2)',
    'IF 1<2 AND 2<3 THEN PRINT "yes" ELSE PRINT "no"',
    'FOR I=1 TO 3: PRINT I;: NEXT I',
    'DIM A(5): FOR I=0 TO 5: A(I)=I*I: NEXT I: PRINT A(4)',
    '10 FOR I=1 TO 3: GOSUB 100: NEXT I: END',
    '100 PRINT I*10;: RETURN',
    'RUN',
    'NEW', '20 DATA 5,6,7', '30 READ X: READ Y: PRINT X; Y', 'RUN',
    'PRINT 1E10, 123456789, 1.5E-7, 100000, 1000000, 10000000',
    'PRINT 1/0',
    'A = = 3',
    'SAVE "/t.bas"', 'NEW', 'LOAD "/t.bas"', 'LIST 10-20') + OFF)
check(re.search(r'PRINT 1\+2.*\n 3\s+2\.5\s+1024\s+-5\s+0\.229\s+0\.333333', out) is not None, 'BASIC: arithmetic and the number format', out[-3000:])
check(re.search(r'\n 5\s+-3\s+7\s+-1\s', out) is not None, 'BASIC: SQR INT ABS SGN')
check('0.841471' in out and '0.540302' in out and '0.785398' in out and '2.71828' in out and '2.30259' in out, 'BASIC: SIN COS ATN EXP LOG')
check('abcd 5  65 Bab' in out.replace('\r', '') or re.search(r'abcd 5\s+65\s+Babefcd', out) is not None, 'BASIC: string functions', out[-2500:])
check(re.search(r'\nyes\n', out) is not None, 'BASIC: IF ... THEN ... ELSE with AND')
check(re.search(r'\n 1\s+2\s+3\s*\n', out) is not None, 'BASIC: FOR / NEXT')
check(re.search(r'\n 16 \n', out) is not None, 'BASIC: arrays')
check(re.search(r'\n 10\s+20\s+30\s*\n', out) is not None, 'BASIC: GOSUB / RETURN and RUN')
check(re.search(r'\n 5  6 \n', out) is not None, 'BASIC: DATA / READ')
check('1E+10' in out and '1.23457E+08' in out and '1.5E-07' in out and ' 100000 ' in out and '1E+06' in out and '1E+07' in out, 'BASIC: big and small numbers')
check('?Division by zero' in out and '?Syntax error' in out, 'BASIC: error messages')
check(re.search(r'LIST 10-20\s+20 DATA 5,6,7', out) is not None, 'BASIC: SAVE, NEW, LOAD and LIST')

steps = BOOT + ['send "basic /bas/mandel.bas\\r"',
                'expect "000000011111111111111111122222233347E7AB322222111100000000000000000000000000000"',
                'expect "000001111111111111111122222222333557BF75433222211111000000000000000000000000000"',
                'expect "000111111111111111112222222233445C      643332222111110000000000000000000000000"']
out, rc = run(disk, steps)
check(rc == 0 or 'timeout' not in out, 'BASIC: mandel.bas draws the first rows of the Mandelbrot set correctly', out[-600:])

# ---- file system operations
steps = BOOT + sh(
    'echo hello world >/t1.txt', 'cat /t1.txt', 'echo second >>/t1.txt', 'cat < /t1.txt',
    'mkdir /d1', 'mkdir /d1/sub', 'cp /t1.txt /d1/sub/copy.txt', 'ls /d1/sub', 'mv /d1/sub/copy.txt /d1/moved.txt',
    'ls -l /d1', 'rm /d1/moved.txt', 'ls /d1', 'cd /d1', 'pwd', 'cd ..', 'pwd', 'rm /d1/sub', 'rm /d1', 'ls /',
    'mkdir /d1/x', 'rm /nofile', 'cat /t1.txt /t1.txt', prompt=P) + OFF
out, rc = run(disk, steps)
check('hello world' in out and 'second' in out, 'redirection with > and >> creates and extends a file', out)
check('COPY.TXT' in out, 'cp into a new subdirectory')
check(re.search(r'MOVED\.TXT\s+\d+', out) is not None, 'mv renames across directories, ls -l shows the size')
check(re.search(r'/D1> .*\n/D1\n', out) is not None or '/D1\n' in out, 'cd and pwd')
check('mkdir: /d1/x: not found' in out, 'mkdir in a missing directory fails', out)
check('rm: /nofile: not found' in out, 'rm of a missing file fails')

# the volume must be consistent and contain the files
vol = os.path.join(work, 'vol.img')
fatlib.Volume(disk).write_volume(vol)
fs = subprocess.run(['fsck.fat', '-n', vol], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(os.environ, LC_ALL='C'))
fsout = fs.stdout.decode('latin-1')
check(fs.returncode == 0, 'fsck.fat finds the volume consistent after the operations', fsout)
v = fatlib.Volume(disk)
check(v.read('/T1.TXT') == b'hello world\nsecond\n', 'the file content on the disk is right (append kept the old data)', repr(v.read('/T1.TXT')))
check(v.find('/D1') is None, 'the removed directories are gone')

# ---- ^C stops the foreground program; a big file is copied intact; two programs at the same time are refused
import random
rnd = random.Random(5)
bigdata = bytes(rnd.randrange(256) for _ in range(150000))
tree = os.path.join(work, 'tree2')
shutil.copytree(os.path.join(BUILD, 'tree'), tree, ignore=shutil.ignore_patterns('skk'))
open(os.path.join(tree, 'big.bin'), 'wb').write(bigdata)
disk2 = os.path.join(work, 'disk2.img')
subprocess.run([sys.executable, '-X', 'utf8', os.path.join(MK, 'tools', 'mkdisk.py'), '-o', disk2, '--boot', os.path.join(BUILD, 'boot.img'),
                '--size-mb', '16', tree], check=True, stdout=subprocess.DEVNULL)
steps = BOOT + sh('spin') [:1] + ['expect "spinning"', 'sleep 300', 'send "\\x03"', 'expect "> "'] + sh('ps', 'cp /big.bin /copy.bin', 'ls -l', 'wc /copy.bin', 'cat /big.bin >/c2.bin', prompt='/> ') + OFF
out, rc = run(disk2, steps, timeout=900)
check('spinning (^C stops)' in out and re.search(r'\^C|> ', out.split('spinning')[-1]) is not None, '^C stops a running program and the shell is back', out[-300:])
check('spin' not in out.split('TID')[-1] if 'TID' in out else False, 'the stopped program is gone from the task list', out[-600:])
vol2 = fatlib.Volume(disk2)
check(vol2.read('/COPY.BIN') == bigdata, 'cp copies a 150000 byte file (74 clusters, FAT chain) intact')
check(vol2.read('/C2.BIN') == bigdata, 'cat > file copies it as well (redirected standard output)')
vol = os.path.join(work, 'vol2.img')
vol2.write_volume(vol)
fs = subprocess.run(['fsck.fat', '-n', vol], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(os.environ, LC_ALL='C'))
check(fs.returncode == 0, 'fsck.fat finds the second volume consistent', fs.stdout.decode('latin-1'))

# ---- Japanese file names (the bytes of the console's character set are kept in the 8.3 name) and text
jdisk = os.path.join(work, 'jdisk.img')
shutil.copy(IMG, jdisk)
out, rc = run(jdisk, BOOT + sh('echo 日本語 >/メモ.txt', 'ls', 'cat /メモ.TXT', 'cp /メモ.txt /写真.bak', 'ls', prompt='/> ') + OFF, extra=['-kanji', 'utf8'])
check('メモ.TXT' in out and '写真.BAK' in out and '日本語' in out.split('cat ')[-1], 'file names and text in Japanese (UTF-8) work', out[-400:])
jv = fatlib.Volume(jdisk)
check(jv.read('/' + 'メモ.TXT'.encode('utf-8').decode('latin-1')) == '日本語\n'.encode('utf-8'), 'the Japanese file is on the disk with the bytes of the name', repr(jv.listdir('/')))

# ---- the kernel refuses wrong system calls; a program whose stack pointer leaves its memory is stopped
out, rc = run(disk, BOOT + sh('faults', 'ps', 'echo alive', prompt='/> ') + OFF)
want = ['send to task 99: 2', 'send to a task that is not there: 2', 'send to myself: 2', 'message outside my memory: 4',
        'grant outside my memory: 4', 'copyto without a client: 1', 'create a task: 1', 'kill a task: 1', 'hook the tick: 1',
        'receive from task 99: 2']
check(all(w in out for w in want), 'the kernel returns the right errors for wrong system calls (bad task, bad address, no right)', out[-900:])
check('alive' in out and re.search(r'\n\s*6\s+running', out) is not None, 'a wild stack pointer stops that program only: the system goes on', out[-400:])

# ---- the file system server as compiled for the 6809, with random operations (apps/fstest.c)
disk3 = os.path.join(work, 'disk3.img')
shutil.copy(IMG, disk3)
out, rc = run(disk3, ['timeout 900000'] + BOOT + sh('fstest 7 70', 'fstest 12 40', prompt='/> ') + OFF, timeout=1200)
check(out.count('fstest: ok') == 2, 'fstest: random file operations, a 60 entry directory and a 70000 byte file, two runs', out[-500:])
vol = os.path.join(work, 'vol3.img')
fatlib.Volume(disk3).write_volume(vol)
fs = subprocess.run(['fsck.fat', '-n', vol], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(os.environ, LC_ALL='C'))
check(fs.returncode == 0 and fatlib.Volume(disk3).find('/FSTEST') is None, 'the volume is consistent after fstest and the test files are gone', fs.stdout.decode('latin-1'))

shutil.rmtree(work, ignore_errors=True)
print('%d checks, %d failed' % (total, fails))
sys.exit(1 if fails else 0)
