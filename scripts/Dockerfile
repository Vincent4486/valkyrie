# SPDX-License-Identifier: BSD-3-Clause

# syntax=docker/dockerfile:1
FROM debian:stable-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    scons python3 python3-sh build-essential \
    gcc g++ make libmpfr-dev libgmp-dev \
    libmpc-dev coreutils grep dosfstools \
    e2fsprogs xorriso grub-pc-bin guestfish \
    qemu-system-x86 gdb texinfo git

WORKDIR /build

# Mount the host CWD to /build during this specific RUN command
RUN --mount=type=bind,source=.,target=/build \
    scons toolchain

RUN rm -rf /var/lib/apt/lists/*
