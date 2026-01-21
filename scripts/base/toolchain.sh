#!/bin/bash 
# SPDX-License-Identifier: BSD-3-Clause

BINUTILS_VERSION=2.37
GCC_VERSION=15.2.0
MUSL_VERSION=1.2.5

BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VERSION}.tar.xz"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
MUSL_URL="https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz"

# ---------------------------

set -e

# Detect OS
OS=$(uname -s)

TOOLCHAINS_DIR=toolchain
OPERATION='build'

while test $# -gt 0
do
    case "$1" in
        -c) OPERATION='clean'
            ;;
        *)  TOOLCHAINS_DIR="$1"
            ;;
    esac
    if [ -n "$2" ] && [[ ! "$2" =~ ^- ]]; then
        TARGET="$2"
        shift
    fi
    shift
done

if [ -z "$TOOLCHAINS_DIR" ]; then
    echo "Missing arg: toolchains directory"
    exit 1
fi

mkdir -p "$TOOLCHAINS_DIR"
cd "$TOOLCHAINS_DIR"
TOOLCHAIN_PREFIX="$(pwd)/${TARGET}"

# Convert i686-elf to i686-linux-musl for musl build
MUSL_TARGET="${TARGET%-elf}-linux-musl"

# ---------------------------

build_binutils() {
    local BINUTILS_SRC="binutils-${BINUTILS_VERSION}"
    local BINUTILS_BUILD="binutils-build-${BINUTILS_VERSION}-${TARGET}"

    if [ ! -d "${BINUTILS_SRC}" ]; then
        wget ${BINUTILS_URL}
        tar -xf binutils-${BINUTILS_VERSION}.tar.xz
    fi

    mkdir -p ${BINUTILS_BUILD}
    cd ${BINUTILS_BUILD}
    CFLAGS= ASMFLAGS= CC= CXX= LD= ASM= LINKFLAGS= LIBS= 
    local BINUTILS_OPTS="--prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --with-sysroot=${TOOLCHAIN_PREFIX} --disable-nls --disable-werror"
    if [ "$OS" = "Darwin" ]; then
        BINUTILS_OPTS="$BINUTILS_OPTS --with-system-zlib --enable-zstd"
    fi
    ../binutils-${BINUTILS_VERSION}/configure $BINUTILS_OPTS
    make -j8 
    make install

    cd ..
}

build_gcc_stage1() {
    local GCC_SRC="gcc-${GCC_VERSION}"
    local GCC_BUILD="gcc-build-${GCC_VERSION}-${TARGET}"

    if [ ! -d "${GCC_SRC}" ]; then
        wget ${GCC_URL}
        tar -xf gcc-${GCC_VERSION}.tar.xz
    fi
    mkdir -p ${GCC_BUILD}
    CFLAGS= ASMFLAGS= LD= ASM= LINKFLAGS= LIBS= 
    cd ${GCC_BUILD}
    local GCC_OPTS="--prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --disable-nls --enable-languages=c --without-headers"
    if [ "$OS" = "Darwin" ]; then
        GCC_OPTS="$GCC_OPTS --with-system-zlib --with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr --with-mpc=/opt/homebrew/opt/libmpc"
    fi
    ../gcc-${GCC_VERSION}/configure $GCC_OPTS
    make -j8 all-gcc all-target-libgcc
    make install-gcc install-target-libgcc

    cd ..
}

build_musl() {
    local MUSL_SRC="musl-${MUSL_VERSION}"
    local MUSL_BUILD="musl-build-${MUSL_VERSION}-${TARGET}"

    if [ ! -d "${MUSL_SRC}" ]; then
        wget ${MUSL_URL}
        tar -xf musl-${MUSL_VERSION}.tar.gz
    fi
    mkdir -p ${MUSL_BUILD}
    cd ${MUSL_BUILD}
    
    # Export toolchain to PATH so musl finds cross-compiler
    export PATH="${TOOLCHAIN_PREFIX}/bin:${PATH}"
    export CC="${TARGET}-gcc"
    export CXX="${TARGET}-g++"
    export LD="${TARGET}-ld"
    export AR="${TARGET}-ar"
    export RANLIB="${TARGET}-ranlib"
    export STRIP="${TARGET}-strip"
    
    CFLAGS= ASMFLAGS= LINKFLAGS= LIBS=
    ../musl-${MUSL_VERSION}/configure \
            --prefix="${TOOLCHAIN_PREFIX}/usr" \
            --host=${MUSL_TARGET} \
            --enable-static \
            --disable-shared
    make -j8
    make install

    cd ..
}

build_gcc_stage2() {
    local GCC_BUILD_STAGE2="gcc-build-stage2-${GCC_VERSION}-${TARGET}"

    mkdir -p ${GCC_BUILD_STAGE2}
    
    # Export toolchain to PATH so GCC stage 2 finds binutils
    export PATH="${TOOLCHAIN_PREFIX}/bin:${PATH}"
    
    CFLAGS= ASMFLAGS= LD= ASM= LINKFLAGS= LIBS= 
    cd ${GCC_BUILD_STAGE2}
    local GCC_OPTS="--prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --with-sysroot=${TOOLCHAIN_PREFIX} --disable-nls --enable-languages=c"
    if [ "$OS" = "Darwin" ]; then
        GCC_OPTS="$GCC_OPTS --with-system-zlib --with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr --with-mpc=/opt/homebrew/opt/libmpc"
    fi
    ../gcc-${GCC_VERSION}/configure $GCC_OPTS
    make -j8
    make install

    cd ..
}

# ---------------------------

# build_binutils
# build_gcc_stage1
build_musl
# build_gcc_stage2