#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""fix_org.py IN OUT : move the data-area block ("org 0" ... "size equ .") of an OS-9
program in front of its "name fcs" line.

a09 numbers code relative to the last 'org'; a few NitrOS-9 sources (help, error)
put the module name before the data area, so a09 writes a wrong execution offset and
module size into the header (18 bytes too small) and the program runs garbage."""
import re, sys
src = open(sys.argv[1], encoding='latin1').read().split('\n')
org = next(i for i, l in enumerate(src) if re.match(r'\s+org\s+0\b', l, re.I))
end = next(i for i in range(org, len(src)) if re.match(r'size\s+equ\s+\.', src[i], re.I))
name = next(i for i, l in enumerate(src) if re.match(r'name\s+fcs', l, re.I))
if name < org:                      # name comes first: move the data block above it
    block = src[org:end + 1]
    rest = src[:org] + src[end + 1:]
    name = next(i for i, l in enumerate(rest) if re.match(r'name\s+fcs', l, re.I))
    src = rest[:name] + block + rest[name:]
open(sys.argv[2], 'w', encoding='latin1').write('\n'.join(src))
