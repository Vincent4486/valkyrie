<p align="center">
  <img src="Documentation/assets/ValeciumOS.png" alt="ValeciumOS Element Block" width="150"/>
</p>

<h1 align="center">ValeciumOS</h1>

Valecium OS is a Unix-like operating system project focused on low-level systems design, modular kernel architecture, and educational clarity.

This repository contains:

- A kernel with architecture-specific and architecture-agnostic layers
- Filesystem, memory, process, and syscall subsystems
- Small userspace components (`libmath`, `sh`)
- Build tooling around SCons and the ValeciumOS Docker builder

## Documentation

Primary source docs live in [`Documentation/`](Documentation/).

- Project index: [`Documentation/index.ad`](Documentation/index.ad)
- Build guide: [`Documentation/building.ad`](Documentation/building.ad)
- Development guide and coding conventions: [`Documentation/devel.ad`](Documentation/devel.ad)
- Implementation roadmap: [`Documentation/roadmap.ad`](Documentation/roadmap.ad)

Published docs are available at [docs.vyang.org/valecium](https://docs.vyang.org/valecium/).

## Quick Start

### 1. Pull the builder image

```bash
docker pull vincent4486/valeciumos-builder:i686
```

### 2. Build

```bash
docker run --rm -v "$PWD:/build" -w /build \
  vincent4486/valeciumos-builder:i686 \
  scons -Q BuildArch=i686
```

### 3. Build specific artifacts

```bash
docker run --rm -v "$PWD:/build" -w /build \
  vincent4486/valeciumos-builder:i686 \
  scons -Q BuildArch=i686 BuildType=kernel

docker run --rm -v "$PWD:/build" -w /build \
  vincent4486/valeciumos-builder:i686 \
  scons -Q BuildArch=i686 BuildType=image ImageFormat=iso
```

Notes:

- Build settings are persisted in `.config` at repository root (auto-created on first run).
- Host builds are possible when the required cross-toolchain and image tools are already installed, but Docker is the supported reproducible path.
- On macOS, build with Docker and run the generated disk image with host QEMU. See [`Documentation/building.ad`](Documentation/building.ad).
- QEMU and GDB helpers live under `scripts/base/` for manual use; SCons does not provide `run` or `debug` targets.

## Repository Guide

Top-level directories and what they contain:

- [`kernel/`](kernel/) - kernel source: CPU, memory, drivers, filesystems, syscalls, HAL, init
- [`include/`](include/) - public and internal headers used by kernel/userspace
- [`usr/`](usr/) - userspace components and libraries
- [`image/`](image/) - disk image assembly logic and root image content
- [`scripts/`](scripts/) - helper scripts for dependencies, toolchain, QEMU, GDB, formatting
- [`Documentation/`](Documentation/) - AsciiDoc source for project documentation

Important build files:

- [`SConstruct`](SConstruct) - root build entrypoint and global configuration
- [`kernel/SConscript`](kernel/SConscript) - kernel build rules
- [`usr/SConscript`](usr/SConscript) - userspace build rules
- [`image/SConscript`](image/SConscript) - image build rules

## Build Configuration Reference

Common SCons variables:

- `BuildConfig`: `debug` (default) or `release`
- `BuildArch`: `i686` (default), `x86_64`, `aarch64`
- `BuildType`: `full` (default), `kernel`, `usr`, `image`
- `ImageFs`: `fat12`, `fat16`, `fat32` (default), `ext2`
- `ImageSize`: image size (for example `250m`)
- `ImageFormat`: `img` (default) or `iso`
- `BootType`: `bios` (default) or `efi`
- `DiskPartitionMap`: `mbr` (default) or `gpt`
- `ImageName`: base name of output image

Examples:

```bash
scons BuildConfig=release
scons BuildArch=x86_64 BuildConfig=release
scons BuildType=kernel
scons ImageFormat=iso
scons BootType=efi DiskPartitionMap=gpt BuildArch=x86_64
```

For full build and platform notes, read [`Documentation/building.ad`](Documentation/building.ad).

## Development and Contributing

Start with [`Documentation/devel.ad`](Documentation/devel.ad), which covers:

- Build/development workflow
- Git and patch submission expectations
- Coding style, naming, and commenting conventions
- Architecture separation and HAL strategy (no platform-specific assembly in common code)

If you plan a larger feature, check [`Documentation/roadmap.ad`](Documentation/roadmap.ad) to align with current priorities.

## Project Status

Valecium OS is actively developed. Roadmap highlights include:

- Core kernel infrastructure improvements
- Expanded syscall coverage
- Filesystem and device capability growth
- Longer-term advanced features (networking, extended process model)

Track current progress in [`Documentation/roadmap.ad`](Documentation/roadmap.ad).

## Licensing

Project licensing summary is in [`COPYING`](COPYING). Individual license texts are in [`LICENCES/`](LICENCES/).

## Support

If you find a bug or want to request a feature, open an issue on the GitHub repository and include:

- Target architecture/config
- Build command used
- Relevant logs or reproduction steps
