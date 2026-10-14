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
