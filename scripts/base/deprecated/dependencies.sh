#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause

DEPS_DEBIAN=(
    libmpfr-dev
    libgmp-dev
    libmpc-dev
    gcc
    python3
    scons
    python3-sh
    dosfstools
)

DEPS_FEDORA=(
    mpfr-devel
    gmp-devel
    libmpc-devel
    gcc
    python3
    scons
    python3-sh
    dosfstools
)

DEPS_ARCH=(
    mpfr
    gmp
    mpc
    gcc
    python
    scons
    python-sh
    dosfstools
)

DEPS_SUSE=(
    mpfr-devel
    gmp-devel
    libmpc-devel
    gcc
    python3
    scons
    python3-sh
    dosfstools
)

DEPS_ALPINE=(
    mpfr-dev 
    mpc-dev 
    gmp-dev
    gcc
    python3
    scons
    py3-sh
    dosfstools
)

OS=
PACKAGE_UPDATE=
PACKAGE_INSTALL=
DEPS=

# Detect distro
if [ -x "$(command -v apk)" ]; then
    OS='alpine'
    PACKAGE_UPDATE='apk update'
    PACKAGE_INSTALL='apk add'
    DEPS="${DEPS_ALPINE[@]}"
elif [ -x "$(command -v apt-get)" ]; then
    OS='debian'
    PACKAGE_UPDATE='apt-get update'
    PACKAGE_INSTALL='apt-get install'
    DEPS="${DEPS_DEBIAN[@]}"
elif [ -x "$(command -v dnf)" ]; then
    OS='fedora'
    PACKAGE_INSTALL='dnf install'
    DEPS="${DEPS_FEDORA[@]}"
elif [ -x "$(command -v yum)" ]; then
    OS='fedora'
    PACKAGE_INSTALL='yum install'
    DEPS="${DEPS_FEDORA[@]}"
elif [ -x "$(command -v zypper)" ]; then
    OS='suse'
    PACKAGE_INSTALL='zypper install'
    DEPS="${DEPS_SUSE[@]}"
elif [ -x "$(command -v pacman)" ]; then
    OS='arch'
    PACKAGE_UPDATE='pacman -Syu'
    PACKAGE_INSTALL='pacman -S'
    DEPS="${DEPS_ARCH[@]}"
else
    echo "Unknown operating system!"; 
    exit 1
fi

# Install dependencies
echo ""
echo "Will install dependencies by running the following command."
echo ""
if [ ! -z "$PACKAGE_UPDATE" ]; then
    echo " $ $PACKAGE_UPDATE"
fi
echo " $ $PACKAGE_INSTALL ${DEPS[@]}"
echo ""

read -p "Continue (y/n)?" choice
case "$choice" in 
  y|Y ) ;;
  * ) echo "Exiting..."
        exit 0
        ;;
esac

if [ ! -z "$PACKAGE_UPDATE" ]; then
    $PACKAGE_UPDATE
fi

$PACKAGE_INSTALL ${DEPS[@]}
