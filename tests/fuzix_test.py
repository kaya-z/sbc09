#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of FUZIX on the simulator (fuzix/, docs/FUZIX.md).

Boots the prebuilt disk image (fuzix/images/sim09-fuzix.dsk.gz, built by fuzix/build.sh) with the boot
ROM on the 64K machine of the simulator, logs in on the console and checks the system: the boot messages,
the partition table (root + swap), the shell and its commands (fork and exec through the swapper), file
creation, pipes, the clock, /dev/rtc, an orderly halt (the simulator powers off) and that the files written
survive a restart.    Run:  tests/fuzix_test.py [-v]
"""
import gzip, os, re, shutil, subprocess, sys, tempfile, datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIM = os.path.join(ROOT, 'sim09')
VERBOSE = '-v' in sys.argv
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     '))


ROM = os.path.join(ROOT, 'fuzix', 'images', 'bootrom.bin')
DSK = os.path.join(ROOT, 'fuzix', 'images', 'sim09-fuzix.dsk.gz')
if not (os.path.exists(ROM) and os.path.exists(DSK)):
    print('SKIP: fuzix/images is empty (run fuzix/build.sh)')
    sys.exit(0)


def run(disk, steps, extra=(), timeout=300):
    """steps: script lines; returns (output, return code)"""
    d = tempfile.mkdtemp(prefix='fuzixt_')
    script = os.path.join(d, 's.txt')
    with open(script, 'w', encoding='utf-8') as f:
        f.write('timeout 120000\n')
        for s in steps:
            f.write(s + '\n')
    cmd = [SIM, '-max', '-rom', ROM, '-0', disk] + list(extra) + ['-script', script]
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                       env=dict(os.environ, LC_ALL='C'))
    shutil.rmtree(d, ignore_errors=True)
    out = p.stdout.decode('utf-8', 'replace').replace('\r', '')
    if VERBOSE:
        print(out)
    return out, p.returncode


def cmds(*lines):
    r = []
    for l in lines:
        r.append('send "%s\\r"' % l)
        r.append('expect "# "')
    return r


work = tempfile.mkdtemp(prefix='fuzixd_')
disk = os.path.join(work, 'disk.dsk')
with gzip.open(DSK, 'rb') as g, open(disk, 'wb') as o:
    shutil.copyfileobj(g, o)

LOGIN = ['expect "login:"', 'send "root\\r"', 'expect "# "']

# --- boot, a few commands, halt
out, rc = run(disk, LOGIN + cmds('echo hello from fuzix', 'ls /', 'mkdir /tmp/t', 'echo data >/tmp/t/f', 'cp /tmp/t/f /tmp/t/g',
                                  'cat /tmp/t/g', 'ls / | cat', 'ps', 'df', 'date', 'sync') +
              ['send "shutdown\\r"', 'expect "Halted."', 'sleep 500'])
check('FUZIX version' in out and 'Fuzix' in out, 'the kernel boots from the boot ROM and prints its banner', out[:300])
check('64KiB total RAM' in out and re.search(r'\d+KiB available to processes', out), 'the memory is 64KiB, the processes get the part above the kernel', out[-600:])
m = re.search(r'(\d+)KiB available', out)
check(m and int(m.group(1)) >= 28, 'at least 28KiB are available to processes (%s)' % (m.group(1) if m else '?'))
check('hda: hda1 hda2 (swap)' in out, 'the partition table is read: root and swap partitions found')
check('Mounting root fs (root_dev=1, ro): OK' in out and 'Starting /init' in out, 'the root file system is mounted and init runs')
check('Current date is' not in out, 'the clock comes from the simulator: no date/time questions at boot')
check('hello from fuzix' in out, 'the shell runs echo')
check(re.search(r'drwx\S*\s+\d+ root\s+root.*\s(bin|etc|tmp)\s*\n', out) is None and all(x in out for x in ('bin', 'dev', 'etc', 'tmp', 'usr')),
      'ls lists the root directory')
check(re.search(r'# cat /tmp/t/g\ndata\n', out) is not None, 'files can be created, copied and read (mkdir, redirection, cp, cat)', out[-900:])
check(re.search(r'# ls / \| cat\n(.*\n)*?bin\n', out) is not None, 'a pipe between two processes works')
check(re.search(r'PID\s+TTY\s+TIME CMD', out) is not None and '-sh' in out, 'ps shows the shell')
check(re.search(r'/dev/hda1\s+\d+\s+\d+\s+\d+', out) is not None, 'df reports the root file system')
check(str(datetime.date.today().year) in out.split('# date')[-1][:60], 'date shows the date of the simulated machine')
check('Halted.' in out and rc == 0, 'shutdown (init stops the system) halts and the simulator powers off (exit code 0)', 'rc=%s' % rc)

# --- the written file survives a restart, the file system was unmounted cleanly by shutdown
out2, rc2 = run(disk, LOGIN + cmds('cat /tmp/t/f', 'ls /tmp/t') + ['send "halt\\r"', 'expect "Halted."', 'sleep 500'])
check(re.search(r'# cat /tmp/t/f\ndata\n', out2) is not None and 'not cleanly' not in out2,
      'after shutdown and a new boot the file is there and the file system was clean', out2[-500:])
check('Halted.' in out2 and rc2 == 0, 'halt (the quick way) also powers off the simulator', 'rc=%s' % rc2)

# --- a second disk
second = os.path.join(work, 'second.img')
with open(second, 'wb') as f:
    f.write(bytes(1024 * 1024))
out3, rc3 = run(disk, LOGIN + ['send "halt\\r"', 'expect "Halted."', 'sleep 500'], extra=['-1', second])
check('hdb:' in out3, 'a second disk image (-1) appears as hdb', out3[-400:])

shutil.rmtree(work, ignore_errors=True)
print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
