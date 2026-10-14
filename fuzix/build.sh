#!/bin/sh
# SPDX-License-Identifier: MIT
# Build FUZIX for the sim09 simulator: kernel, libraries, a minimal set of commands, the root
# file system, and the disk image to boot with  ./sim09 -rom fuzix/images/bootrom.bin -0 DISK
#
#   fuzix/build.sh WORKDIR
#
# Needs on PATH: the m6809 gcc 4.6.4 (tools/build_gcc6809.sh) and lwtools (tools/install_cmoc.sh),
# perl, python3, gcc, make, yacc (bison -y), git and network access to GitHub for the FUZIX sources.
# Results: fuzix/images/bootrom.bin and fuzix/images/sim09-fuzix.dsk (and .gz).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${1:?usage: build.sh WORKDIR}
FUZIX_URL=https://github.com/EtchedPixels/FUZIX.git
FUZIX_REV=776bfc942c0ce3f5b86e4c48848d154a60ff1cb6       # 2025-09-02, the version this port was made with
ISIZE=${ISIZE:-128}                                      # inodes (in units of 16 per block)
BSIZE=${BSIZE:-4096}                                     # blocks of the root file system (2 MB)
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
for t in m6809-unknown-gcc lwasm lwlink perl python3 yacc git; do
    command -v $t >/dev/null || { echo "missing: $t"; exit 1; }
done

if [ ! -d "$WORK/FUZIX/.git" ]; then
    mkdir -p "$WORK/FUZIX" && cd "$WORK/FUZIX"
    git init -q . && git remote add origin "$FUZIX_URL"
    git fetch -q --depth 1 origin "$FUZIX_REV" && git checkout -q FETCH_HEAD
fi
cd "$WORK/FUZIX"
[ "$(git rev-parse HEAD)" = "$FUZIX_REV" ] || { echo "$WORK/FUZIX is not at $FUZIX_REV"; exit 1; }

# the platform
rm -rf Kernel/platform/platform-sim09
cp -r "$HERE/platform-sim09" Kernel/platform/platform-sim09

export FUZIX_ROOT=$PWD CPU=6809 USERCPU=6809 TARGET=sim09 YACC=yacc
export PATH="$PWD/Build/tools:$PATH"
J=${JOBS:-4}
make TARGET=sim09 YACC=yacc kclean >/dev/null 2>&1 || true
make -j$J TARGET=sim09 YACC=yacc ltools libs stand kernel
(cd Applications && make -k util cmd sh)

# root file system (big endian, 512 byte blocks) and the disk
(cd Standalone/filesystem-src && ./build-mini-filesystem -X "$WORK/rootfs.img" "$ISIZE" "$BSIZE")
python3 "$HERE/mkdisk.py" -k Kernel/platform/platform-sim09/fuzix.img -f "$WORK/rootfs.img" -o "$WORK/sim09-fuzix.dsk"

mkdir -p "$HERE/images"
lwasm --format=raw -o "$HERE/images/bootrom.bin" "$HERE/bootrom.s"
cp "$WORK/sim09-fuzix.dsk" "$HERE/images/sim09-fuzix.dsk"
gzip -9 -n -c "$HERE/images/sim09-fuzix.dsk" > "$HERE/images/sim09-fuzix.dsk.gz"
echo "done: $HERE/images/bootrom.bin $HERE/images/sim09-fuzix.dsk"
