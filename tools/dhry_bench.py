#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""dhry_bench.py - cycle exact Dhrystone figures of the simulated machine.

    tools/dhry_bench.py [-1|-2] [--clock HZ] [--runs N1,N2] [--cmds DIR] [--rom FILE]

The OS-9 clock counts whole seconds, so `dhry` is only accurate over long runs.  Here the simulator
itself is the stopwatch: `dhry N1` and `dhry N2` are run in two simulations (-max, -stats) and the
difference of the executed CPU cycles belongs to N2 - N1 passes (booting, the shell and the final
print are the same in both and drop out).  Timer interrupts, the scheduler and the clock are
included, as they are on the real system.
"""
import argparse, os, re, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(level, runs, clock, rom, cmds):
    d = tempfile.mkdtemp(prefix='dhry_')
    try:
        for f in ('OS9.dsk', 'WORK.dsk'):
            shutil.copy(os.path.join(ROOT, 'os9', f), d)
        v0 = os.path.join(d, 'v0')
        os.mkdir(v0)
        shutil.copytree(cmds, os.path.join(v0, 'cmds'))
        script = os.path.join(d, 's.txt')
        with open(script, 'w') as f:
            f.write('timeout 3000000\nexpect "OS9:"\nsend "chx /v0/cmds\\r"\nexpect "OS9:"\n'
                    'send "dhry %d\\r"\nexpect "checks"\nexpect "OS9:"\nquit\n' % runs)
        cmd = [os.path.join(ROOT, 'sim09')] + (['-2'] if level == 2 else []) + [
            '-max', '-stats', '-clock', str(clock), '-rom', rom, '-0', os.path.join(d, 'OS9.dsk'),
            '-1', os.path.join(d, 'WORK.dsk'), '-v', v0, '-script', script]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=dict(os.environ, LC_ALL='C'), timeout=3600)
        out = p.stdout.decode('utf-8', 'replace')
        m = re.search(r'(\d+) instructions, (\d+) cycles', out)
        if not m or 'Dhrystone checks OK' not in out:
            sys.exit('dhry failed:\n' + out[-800:])
        return int(m.group(1)), int(m.group(2))
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group()
    g.add_argument('-1', dest='level', action='store_const', const=1)
    g.add_argument('-2', dest='level', action='store_const', const=2)
    ap.add_argument('--clock', type=int, default=1843200, help='CPU clock in Hz (default 1843200)')
    ap.add_argument('--runs', default='1000,4000', help='the two run counts (default 1000,4000)')
    ap.add_argument('--rom')
    ap.add_argument('--cmds')
    a = ap.parse_args()
    level = a.level or 1
    rom = a.rom or os.path.join(ROOT, 'os9', 'os9v%d.rom' % level)
    cmds = a.cmds or os.path.join(ROOT, 'os9', 'level%d' % level, 'cmds')
    n1, n2 = (int(x) for x in a.runs.split(','))
    i1, c1 = run(level, n1, a.clock, rom, cmds)
    i2, c2 = run(level, n2, a.clock, rom, cmds)
    cyc = (c2 - c1) / (n2 - n1)
    ins = (i2 - i1) / (n2 - n1)
    dps = a.clock / cyc
    print('OS-9 Level %d, CPU clock %.4f MHz (cycles = E clock)' % (level, a.clock / 1e6))
    print('  per pass through Dhrystone: %.0f cycles, %.0f instructions (%.2f cycles/instruction)' % (cyc, ins, cyc / ins))
    print('  Dhrystones per second     : %.1f' % dps)
    print('  DMIPS (Dhrystones/s / 1757): %.3f   = %.3f DMIPS/MHz' % (dps / 1757, dps / 1757 / (a.clock / 1e6)))


if __name__ == '__main__':
    main()
