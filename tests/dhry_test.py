#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Tests of the Dhrystone benchmark (os9/dhry) and of the RTC that times it.

The final values of the benchmark are the ones that the Dhrystone 2.1 sources list as correct;
`dhry` checks them itself ("Dhrystone checks OK").  The time of the timed mode comes from the
OS-9 clock, which follows the emulated time (also with -max), so the result must scale with -clock.
Run:  tests/dhry_test.py [-v]
"""
import os, re, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERBOSE = '-v' in sys.argv
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     '))


def run(level, cmdline, clock=1843200):
    d = tempfile.mkdtemp(prefix='dhryt_')
    try:
        for f in ('OS9.dsk', 'WORK.dsk'):
            shutil.copy(os.path.join(ROOT, 'os9', f), d)
        os.mkdir(os.path.join(d, 'v0'))
        shutil.copytree(os.path.join(ROOT, 'os9', 'level%d' % level, 'cmds'), os.path.join(d, 'v0', 'cmds'))
        script = os.path.join(d, 's.txt')
        with open(script, 'w') as f:
            f.write('timeout 600000\nexpect "OS9:"\nsend "chx /v0/cmds\\r"\nexpect "OS9:"\nsend "%s\\r"\nexpect "OS9:"\nquit\n' % cmdline)
        cmd = [os.path.join(ROOT, 'sim09')] + (['-2'] if level == 2 else []) + [
            '-max', '-clock', str(clock), '-rom', os.path.join(ROOT, 'os9', 'os9v%d.rom' % level),
            '-0', os.path.join(d, 'OS9.dsk'), '-1', os.path.join(d, 'WORK.dsk'), '-v', os.path.join(d, 'v0'), '-script', script]
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(os.environ, LC_ALL='C'),
                             timeout=900).stdout.decode('utf-8', 'replace')
        if VERBOSE:
            print(out)
        return out
    finally:
        shutil.rmtree(d, ignore_errors=True)


for level in (1, 2):
    tag = 'L%d' % level
    out = run(level, 'dhry 300')
    check('Dhrystone checks OK' in out and 'WRONG' not in out, '%s dhry 300: every final value is the one of Dhrystone 2.1' % tag, out[-600:])
    check('Arr_2_Glob[8][7]:    310' in out, '%s Arr_2_Glob[8][7] = runs + 10' % tag)
    out = run(level, 'dhry -t 3')
    m = re.search(r'Dhrystones per second: +(\d+)', out)
    t = re.search(r'Time: +(\d+) s', out)
    dps = int(m.group(1)) if m else 0
    check('Dhrystone checks OK' in out and t and int(t.group(1)) == 3, '%s dhry -t 3 runs for 3 seconds of OS-9 time (also with -max)' % tag, out[-300:])
    check(150 <= dps <= 280, '%s about 210 Dhrystones/s at 1.8432 MHz (got %d)' % (tag, dps))
    if level == 1:
        out = run(level, 'dhry -t 3', clock=3686400)
        m = re.search(r'Dhrystones per second: +(\d+)', out)
        d2 = int(m.group(1)) if m else 0
        check(1.8 <= d2 / max(dps, 1) <= 2.2, 'L1 at twice the clock (-clock 3686400) twice the speed (%d vs %d)' % (d2, dps))

print('%d checks, %d failure(s)' % (total, fails))
sys.exit(1 if fails else 0)
