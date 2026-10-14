#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""profile.py - where does the time go?  Runs sim09 with a script and counts the executed
instructions per function (from the link maps of the modules), without writing a trace file.

    profile.py BUILD_DIR SCRIPT [sim09 options...]      (prints the top functions)"""
import bisect, collections, os, re, subprocess, sys, tempfile

build, script = sys.argv[1], sys.argv[2]
mk = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
syms = []
for f in os.listdir(build):
    if f.endswith('.map'):
        for l in open(os.path.join(build, f)):
            m = re.match(r'Symbol: (\w+) \((\S+)\) = ([0-9A-F]+)', l)
            if m and not re.match(r'(funcend|funcsize|string_literals|bss_|S\d+$|L\d+$|[sl]_|D16|\\)', m.group(1)) and not m.group(1).startswith('.'):
                syms.append((int(m.group(3), 16), f[:-4] + ':' + m.group(1)))
syms.sort()
addrs = [a for a, _ in syms]
d = tempfile.mkdtemp()
fifo = os.path.join(d, 'trace')
os.mkfifo(fifo)
cmd = [os.path.join(os.path.dirname(mk), 'sim09'), '-max', '-rom', os.path.join(build, 'bootrom.bin'), '-0', os.path.join(build, 'mk09.img'),
       '-t', fifo, '-script', script] + sys.argv[3:]
p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=dict(os.environ, LC_ALL='C'))
c = collections.Counter()
callers = collections.Counter()
want = os.environ.get('PROFILE_CALLERS')            # name of a function: who calls it?
entry = set(a for a, n in syms if want and n.endswith(':' + want))
tot = 0
prev = 0
with open(fifo, errors='replace') as f:
    for l in f:
        m = re.search(r'pc=([0-9A-F]{4})', l)
        if m:
            pc = int(m.group(1), 16)
            i = bisect.bisect_right(addrs, pc) - 1
            c[syms[i][1] if i >= 0 else '?'] += 1
            tot += 1
            if pc in entry:
                j = bisect.bisect_right(addrs, prev) - 1
                callers[syms[j][1] if j >= 0 else '?'] += 1
            prev = pc
p.wait()
if want:
    print('callers of', want, callers.most_common(8))
os.unlink(fifo)
os.rmdir(d)
import json
if os.environ.get('PROFILE_JSON'):
    json.dump(c, open(os.environ['PROFILE_JSON'], 'w'))
print('%d instructions' % tot)
for k, v in c.most_common(30):
    print('%8d %5.1f%% %s' % (v, 100.0 * v / tot, k))
