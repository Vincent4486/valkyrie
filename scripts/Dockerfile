# SPDX-License-Identifier: BSD-3-Clause
FROM debian:stable-slim

RUN apt-get update && apt-get install -y \
    scons python3 python3-sh build-essential \
    gcc g++ make libmpfr-dev libgmp-dev \
    libmpc-dev coreutils grep dosfstools \
    xorriso grub-pc-bin guestfish \
    qemu-system-x86 gdb texinfo git \
    && rm -rf /var/lib/apt/lists/*

ENV PATH="/opt/cross/bin:${PATH}"

WORKDIR /build

RUN --mount=type=bind,source=.,target=/build \
    python3 ./scripts/base/toolchain.py /opt/cross -a i686

RUN i686-linux-musl-gcc --version