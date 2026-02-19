#!/bin/bash 
# SPDX-License-Identifier: BSD-3-Clause

BINUTILS_VERSION=2.45
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
TOOLCHAIN_PREFIX=$(pwd)

SYSROOT="${TOOLCHAIN_PREFIX}/${TARGET}/sysroot"

echo "Using toolchain prefix: ${TOOLCHAIN_PREFIX}"

MUSL_TARGET="${TARGET}"

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
    local BINUTILS_OPTS="--prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --with-sysroot=${SYSROOT} --disable-nls --disable-werror"
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
    cd ${GCC_BUILD}
    
    local GCC_OPTS="--prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --with-sysroot=${SYSROOT} --disable-nls --enable-languages=c --without-headers --disable-threads --disable-isl --disable-shared --with-newlib"
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
    export CC="${MUSL_TARGET}-gcc --sysroot=${SYSROOT}"
    export CXX="${MUSL_TARGET}-g++ --sysroot=${SYSROOT}"
    export AS="${MUSL_TARGET}-as"
    export LD="${MUSL_TARGET}-ld"
    export AR="${MUSL_TARGET}-ar"
    export RANLIB="${MUSL_TARGET}-ranlib"
    export STRIP="${MUSL_TARGET}-strip"
    
    ../musl-${MUSL_VERSION}/configure \
            --prefix="${SYSROOT}/usr" \
            --host=${MUSL_TARGET} \
            --enable-static \
            --enable-shared
    make -j8
    make install

    cd ..
}

build_gcc_stage2() {
    local GCC_BUILD="gcc-build-${GCC_VERSION}-${TARGET}"
    mkdir -p ${GCC_BUILD}
    
    # Export toolchain to PATH so GCC stage 2 finds binutils
    export PATH="${TOOLCHAIN_PREFIX}/bin:${PATH}"
    
    # Use host compiler for build; set target tools explicitly for generated runtime libs
    unset CC CXX CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
    export CC_FOR_TARGET="${MUSL_TARGET}-gcc --sysroot=${SYSROOT}"
    export CXX_FOR_TARGET="${MUSL_TARGET}-g++ --sysroot=${SYSROOT}"
    export AR_FOR_TARGET="${MUSL_TARGET}-ar"
    export RANLIB_FOR_TARGET="${MUSL_TARGET}-ranlib"
    export STRIP_FOR_TARGET="${MUSL_TARGET}-strip"
    export CFLAGS_FOR_TARGET="-O2"
    export CXXFLAGS_FOR_TARGET="-O2"
    
    cd ${GCC_BUILD}
    local BUILD_TRIPLE="$(../gcc-${GCC_VERSION}/config.guess)"
    local GCC_OPTS="--build=${BUILD_TRIPLE} --host=${BUILD_TRIPLE} --prefix=${TOOLCHAIN_PREFIX} --target=${TARGET} --with-sysroot=${SYSROOT} --disable-nls --enable-languages=c,c++ --disable-isl --disable-libsanitizer"
    ../gcc-${GCC_VERSION}/configure $GCC_OPTS
    make -j8
    make install

    cd ..
}

# ---------------------------

# build_binutils
# build_gcc_stage1
# build_musl
build_gcc_stage2
