# Force amd64 architecture
FROM --platform=linux/amd64 ubuntu:24.04

# 1. Setup Environment
ENV DEBIAN_FRONTEND=noninteractive
ENV LIBGUESTFS_BACKEND=direct

# 2. Install your exact dependency list
RUN apt-get update && apt-get install -y \
    build-essential \
    libmpfr-dev \
    libgmp-dev \
    libmpc-dev \
    gcc \
    g++ \
    make \
    python3 \
    python3-pip \
    scons \
    guestfish \
    libguestfs-tools \
    grub-pc-bin \
    grub-common \
    grub2-common \
    xorriso \
    mtools \
    clang-format \
    qemu-system-x86 \
    git \
    curl \
    wget \
    linux-image-generic \
    && rm -rf /var/lib/apt/lists/*

# 3. Ensure guestfish can access the kernel appliance
RUN chmod +r /boot/vmlinuz-*

# 4. Clone, Build Toolchain, and Cleanup
# This uses your specific repo to build the cross-compiler
RUN git clone https://github.com/Vincent4486/valkyrie.git /tmp/valkyrie-build && \
    cd /tmp/valkyrie-build && \
    # Using your internal script logic to build the i686-linux-musl toolchain
    scons toolchain && \
    cd / && \
    rm -rf /tmp/valkyrie-build

# 5. Final environment setup
WORKDIR /valkyrie
CMD ["/bin/bash"]