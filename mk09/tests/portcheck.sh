#!/bin/sh
# SPDX-License-Identifier: MIT
# Portability check: the machine independent C of MK09 (the kernel, the servers, the file system
# code, the library) must compile with an ordinary C compiler, without CMOC and without the
# 6809 headers; only arch/<cpu>/arch.h, bsp/<board>/bsp.h and memmap.h are board / CPU specific.
# (This does not run anything: tests/fs_host.c runs the file system code on the host.)
set -e
cd "$(dirname "$0")/.."
B=${B:-build}
[ -f $B/memmap.h ] || make -s $B/memmap.h
CC=${CC:-gcc}
FLAGS="-std=gnu89 -fsyntax-only -Wall -Wextra -Wno-unused-parameter -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -DMK_HOST -Iinclude -Iarch/m6809 -Ibsp/sbc09 -I$B -Iservers -Iapps/jwp"
n=0
for f in kernel/kernel.c servers/fs_core.c servers/fs.c servers/disk.c servers/tty.c servers/pm.c lib/mk_*.c; do
    $CC $FLAGS $f
    n=$((n + 1))
done
echo "portcheck: $n files are plain C"
