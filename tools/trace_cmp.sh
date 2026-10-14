#!/bin/sh
# SPDX-License-Identifier: MIT
# trace_cmp.sh REF.trace MINE.trace : report the first instruction at which two traces differ
cut -d: -f1 "$1" > /tmp/trace_cmp_a.$$
cut -d: -f1 "$2" > /tmp/trace_cmp_b.$$
LC_ALL=C cmp /tmp/trace_cmp_a.$$ /tmp/trace_cmp_b.$$ > /tmp/trace_cmp_c.$$ 2>&1
if [ ! -s /tmp/trace_cmp_c.$$ ]; then echo "traces identical ($(wc -l < /tmp/trace_cmp_a.$$) lines)"; rm -f /tmp/trace_cmp_?.$$; exit 0; fi
n=$(sed 's/.*line //' /tmp/trace_cmp_c.$$)
echo "first difference at line $n"
s=$((n>6 ? n-6 : 1))
echo "--- reference"; sed -n "${s},$((n+1))p" "$1" | cut -c1-120
echo "--- simulator"; sed -n "${s},$((n+1))p" "$2" | cut -c1-120
rm -f /tmp/trace_cmp_?.$$
exit 1
