#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""mkskkdic.py - build the dictionary file read by jwp's SKK input method.

    mkskkdic.py [-e utf8|euc|sjis] [-b KBYTES] [-o skk.dic] SKK-JISYO.L

The output keeps the SKK text format ("よみ /候補1/候補2/") with these changes:
  * one sorted file (byte order of the reading in the guest character set): the okuri-ari
    entries ("かk") and the okuri-nasi entries are merged, so jwp can find an entry with a
    binary search on the disk without loading the dictionary into memory;
  * lines end with CR (the OS-9 line end), the character set is the one of the OS-9 console;
  * annotations (";...") and the Lisp candidates "(concat ...)" are dropped, candidates that
    the character set cannot represent (Shift_JIS) are dropped, a line is at most MAXLINE bytes;
  * with -b the file is cut to about KBYTES kilobytes: the entries with the shortest readings
    are kept first (a plain heuristic: SKK dictionaries carry no frequencies).

The input is an SKK dictionary in EUC-JP or UTF-8 (the coding cookie of its first line decides),
e.g. /usr/share/skk/SKK-JISYO.L of the Debian package skkdic (GPL v2 or later).
"""
import argparse, re, sys

CODECS = {'utf8': 'utf-8', 'euc': 'euc_jp', 'sjis': 'cp932'}
MAXLINE = 600


def read_skk(path):
    raw = open(path, 'rb').read()
    head = raw[:200].decode('ascii', 'replace')
    m = re.search(r'coding:\s*([\w-]+)', head)
    coding = (m.group(1) if m else 'euc-jp').lower().replace('euc-jp', 'euc_jp')
    text = raw.decode(coding, 'replace')
    entries = []
    for line in text.split('\n'):
        if not line or line.startswith(';') or ' ' not in line:
            continue
        key, rest = line.split(' ', 1)
        cands = []
        for c in rest.strip().split('/'):
            c = c.split(';')[0]
            if c and not c.startswith('(') and c not in cands:
                cands.append(c)
        if cands:
            entries.append((key, cands))
    return entries


def build(entries, enc, budget=None):
    codec = CODECS[enc]
    out = {}
    for key, cands in entries:
        try:
            kb = key.encode(codec)
        except UnicodeEncodeError:
            continue
        cb = []
        for c in cands:
            try:
                b = c.encode(codec)
            except UnicodeEncodeError:
                continue
            if b'\r' in b or b'\n' in b or b'/' in b:
                continue
            cb.append(b)
        if not cb:
            continue
        line = kb + b' /'
        for b in cb:
            if len(line) + len(b) + 2 > MAXLINE:
                break
            line += b + b'/'
        if kb in out:                      # the same reading twice: keep the first
            continue
        out[kb] = line
    items = list(out.items())
    if budget:
        items.sort(key=lambda kv: (len(kv[0]), kv[0]))
        kept, size = [], 0
        for k, line in items:
            if size + len(line) + 1 > budget:
                break
            kept.append((k, line))
            size += len(line) + 1
        items = kept
    items.sort()
    return b''.join(line + b'\r' for _, line in items), len(items)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', help='SKK-JISYO.L or another SKK dictionary')
    ap.add_argument('-e', '--enc', choices=sorted(CODECS), default='utf8')
    ap.add_argument('-b', '--budget', type=int, default=0, help='size limit in kilobytes (default: everything)')
    ap.add_argument('-o', '--output', default='skk.dic')
    a = ap.parse_args(argv)
    data, n = build(read_skk(a.src), a.enc, a.budget * 1024 if a.budget else None)
    with open(a.output, 'wb') as f:
        f.write(data)
    print('%s: %d entries, %d bytes (%s)' % (a.output, n, len(data), a.enc))


if __name__ == '__main__':
    main()
