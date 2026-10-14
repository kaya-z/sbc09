#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the m6809 gcc 4.6.4 cross compiler (cc1 and libgcc) that FUZIX needs.
#
#   tools/build_gcc6809.sh WORKDIR PREFIX LWTOOLS_BINDIR
#
# WORKDIR  scratch directory (about 1.5 GB); downloads gcc 4.6.4, GMP, MPFR and MPC there
# PREFIX   where the compiler and the as/ld/ar wrappers are installed (PREFIX/bin)
# LWTOOLS_BINDIR  directory with lwasm, lwlink, lwar (tools/install_cmoc.sh installs them)
#
# gcc 4.6.4 and the 6809 backend patch "gcc6809lw-4.6.4-11" of lwtools (extra/ in the lwtools
# sources; the patch also lets it build with a modern host compiler).  The instructions follow
# extra/README of lwtools.  Only cc1 (language C), the driver and libgcc are built.
set -e
WORK=${1:?usage: build_gcc6809.sh WORKDIR PREFIX LWTOOLS_BINDIR}
PREFIX=${2:?}
LWBIN=${3:?}
LWSRC=${LWSRC:-}                       # lwtools source tree (for extra/); default: next to LWTOOLS_BINDIR
J=${JOBS:-$(nproc)}
GCCV=4.6.4
PATCH=gcc6809lw-4.6.4-11.patch
mkdir -p "$WORK" "$PREFIX/bin"
WORK=$(cd "$WORK" && pwd); PREFIX=$(cd "$PREFIX" && pwd); LWBIN=$(cd "$LWBIN" && pwd)
cd "$WORK"
fetch() { [ -f "$(basename "$1")" ] || curl -fsSL -o "$(basename "$1")" "$1"; }
fetch https://ftp.gnu.org/gnu/gcc/gcc-$GCCV/gcc-$GCCV.tar.bz2
fetch https://ftp.gnu.org/gnu/gmp/gmp-6.3.0.tar.xz
fetch https://ftp.gnu.org/gnu/mpfr/mpfr-4.2.1.tar.xz
fetch https://ftp.gnu.org/gnu/mpc/mpc-1.3.1.tar.gz

# the lwtools extras: wrapper scripts as/ld/ar and the gcc patch
if [ -z "$LWSRC" ]; then
    for d in "$LWBIN/../src/lwtools"* "$LWBIN/../../lwtools"* "$WORK/lwtools"*; do [ -d "$d/extra" ] && LWSRC=$d; done
fi
[ -d "$LWSRC/extra" ] || { echo "set LWSRC to the lwtools source tree (needs extra/)"; exit 1; }

# host libraries, static, private
if [ ! -f "$WORK/hostlibs/lib/libmpc.a" ]; then
    for p in gmp-6.3.0:xz mpfr-4.2.1:xz mpc-1.3.1:gz; do
        n=${p%%:*}; [ -d $n ] || tar xf $n.tar.*
    done
    (cd gmp-6.3.0 && ./configure --prefix="$WORK/hostlibs" --disable-shared >/dev/null && make -j$J >/dev/null && make install >/dev/null)
    (cd mpfr-4.2.1 && ./configure --prefix="$WORK/hostlibs" --disable-shared --with-gmp="$WORK/hostlibs" >/dev/null && make -j$J >/dev/null && make install >/dev/null)
    (cd mpc-1.3.1 && ./configure --prefix="$WORK/hostlibs" --disable-shared --with-gmp="$WORK/hostlibs" --with-mpfr="$WORK/hostlibs" >/dev/null && make -j$J >/dev/null && make install >/dev/null)
fi

[ -d gcc-$GCCV ] || tar xjf gcc-$GCCV.tar.bz2
if [ ! -f gcc-$GCCV/.patched ]; then
    (cd gcc-$GCCV && patch -p1 -s < "$LWSRC/extra/$PATCH") && touch gcc-$GCCV/.patched
fi

# as / ld / ar wrappers in front of lwasm / lwlink / lwar
for t in as ld ar; do cp "$LWSRC/extra/$t" "$PREFIX/bin/m6809-unknown-$t"; chmod +x "$PREFIX/bin/m6809-unknown-$t"; done
for t in nm objdump ranlib strip; do ln -sf /bin/true "$PREFIX/bin/m6809-unknown-$t"; done
export PATH="$PREFIX/bin:$LWBIN:$PATH"

mkdir -p build && cd build
if [ ! -f Makefile ]; then
    ../gcc-$GCCV/configure --enable-languages=c --target=m6809-unknown \
        --program-prefix=m6809-unknown- --enable-obsolete --disable-threads --disable-nls \
        --disable-libssp --disable-multilib --disable-bootstrap --prefix="$PREFIX" \
        --with-gmp="$WORK/hostlibs" --with-mpfr="$WORK/hostlibs" --with-mpc="$WORK/hostlibs" \
        --with-as="$PREFIX/bin/m6809-unknown-as" --with-ld="$PREFIX/bin/m6809-unknown-ld" \
        --with-ar="$PREFIX/bin/m6809-unknown-ar"
fi
make -j$J MAKEINFO=true all-gcc
make MAKEINFO=true install-gcc
make -j$J all-target-libgcc && make install-target-libgcc || echo "libgcc did not build completely (see README)"
echo "installed into $PREFIX"
