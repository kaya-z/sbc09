#!/bin/sh
# SPDX-License-Identifier: MIT
# Instruction-by-instruction cross check of sim09 against the reference
# simulator (sbc09 v09 / v09c, patched by refpatch.py to use an instruction
# counted timer).  Both run the same OS-9 ROM with the same typed input and
# the register trace of every instruction is compared.
#
#   crosscheck.sh [SBC09_DIR]      (default ../sbc09_shinji relative to the project)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
sbc09=${1:-$root/../sbc09_shinji}
work=${TMPDIR:-/tmp}/crosscheck.$$
mkdir -p "$work"
python3 "$here/refpatch.py" "$sbc09/src" "$work/ref" >/dev/null
cp "$root/os9/OS9.dsk" "$root/os9/WORK.dsk" "$work/"
status=0

run() {   # level N START GAP TEXT
  lvl=$1; n=$2; st=$3; gp=$4; tx=$5
  mkdir -p "$work/v$lvl"; rm -rf "$work/v$lvl/cmds"; cp -r "$root/os9/level$lvl/cmds" "$work/v$lvl/cmds"
  if [ "$lvl" = 2 ]; then refbin=refv09c; rom=os9v2.rom; opt=-2; else refbin=refv09; rom=os9v1.rom; opt=; fi
  ( cd "$work" && DETTYPE="$tx" DETSTART=$st DETGAP=$gp DETMAX=$n DETPERIOD=5000 ./ref/$refbin \
      -rom "$root/os9/$rom" -0 OS9.dsk -1 WORK.dsk -v "$work/v$lvl" -t ref.trace </dev/null >ref.out 2>&1 || true
    VDISK_COMPAT=1 VDISK_UNSORTED=1 "$root/sim09" $opt -max -icount 5000 -maxcount $n -dettype $st $gp "$tx" \
      -t mine.trace -rom "$root/os9/$rom" -0 OS9.dsk -1 WORK.dsk -v "$work/v$lvl" </dev/null >mine.out 2>&1 || true )
  # the simulator writes one more trace line than the reference after the last instruction
  head -n $n "$work/mine.trace" > "$work/mine.cut"
  head -n $n "$work/ref.trace"  > "$work/ref.cut"
  printf 'Level %s, %s instructions: ' "$lvl" "$n"
  if "$root/tools/trace_cmp.sh" "$work/ref.cut" "$work/mine.cut" > "$work/cmp.out"; then cat "$work/cmp.out"; else cat "$work/cmp.out"; status=1; fi
}

run 1 1500000 700000 3000 'chx /v0/cmds\rdir -e /v0/cmds\r'
run 2 1500000 800000 3000 'chx /v0/cmds\rmdir -e\r'
rm -rf "$work"
exit $status
