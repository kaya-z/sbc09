#!/bin/sh
# SPDX-License-Identifier: MIT
# Build lwtools (6809 assembler/linker) and CMOC (6809 C compiler) into a prefix,
# needed only to rebuild os9/jwp/jwp from its C source.
#
#   tools/install_cmoc.sh PREFIX [DIR_WITH_TARBALLS]
#   make -C os9/jwp PATH=PREFIX/bin:$PATH
#
# The tarballs are downloaded unless they are already in DIR_WITH_TARBALLS.
set -e
PREFIX=${1:?usage: install_cmoc.sh PREFIX [tarball-dir]}
SRC=${2:-$(mktemp -d)}
LWTOOLS=lwtools-4.25
CMOC=cmoc-0.1.100
LW_URL=http://www.lwtools.ca/releases/lwtools/$LWTOOLS.tar.gz
CMOC_URL=http://sarrazip.com/dev/$CMOC.tar.gz

mkdir -p "$PREFIX" "$SRC"
PREFIX=$(cd "$PREFIX" && pwd)
cd "$SRC"
[ -f $LWTOOLS.tar.gz ] || wget -q "$LW_URL" -O $LWTOOLS.tar.gz
[ -f $CMOC.tar.gz ] || wget -q "$CMOC_URL" -O $CMOC.tar.gz
tar xzf $LWTOOLS.tar.gz
tar xzf $CMOC.tar.gz

make -C $LWTOOLS PREFIX="$PREFIX" >/dev/null
make -C $LWTOOLS PREFIX="$PREFIX" install >/dev/null
PATH="$PREFIX/bin:$PATH"
(cd $CMOC && ./configure --prefix="$PREFIX" >/dev/null && make >/dev/null && make install >/dev/null)
echo "installed: $PREFIX/bin/cmoc  (add $PREFIX/bin to PATH)"
