# Valkyrie OS

Valkyrie OS is a Unix-like operating system project focused on low-level systems design, modular kernel architecture, and educational clarity.

This repository contains:

- A kernel with architecture-specific and architecture-agnostic layers
- Filesystem, memory, process, and syscall subsystems
- Small userspace components (`libmath`, `sh`)
- Build tooling around SCons, QEMU, and a managed cross-toolchain

## Documentation

Primary source docs live in [`Documents/`](Documents/).

- Project index: [`Documents/index.ad`](Documents/index.ad)
- Build guide: [`Documents/building.ad`](Documents/building.ad)
- Development guide and coding conventions: [`Documents/devel.ad`](Documents/devel.ad)
- Implementation roadmap: [`Documents/roadmap.ad`](Documents/roadmap.ad)

Published docs are available at [docs.vyang.org/valkyrie](https://docs.vyang.org/valkyrie/).

## Quick Start

### 1. Install host prerequisites

You need Python 3 and SCons.

macOS example:

```bash
brew install python3 scons
```

### 2. Install dependencies

```bash
scons deps
```

### 3. Build

```bash
scons
```

### 4. Run or debug

```bash
scons run
# or
scons debug
```

Notes:

- Build settings are persisted in `.config` at repository root (auto-created on first run).
- Toolchain setup is handled by the build flow (`--ensure`) and is also available explicitly via `scons toolchain`.
- On macOS, image-building support is limited (notably due to `parted` availability). See [`Documents/building.ad`](Documents/building.ad).

## Repository Guide

Top-level directories and what they contain:

- [`kernel/`](kernel/) - kernel source: CPU, memory, drivers, filesystems, syscalls, HAL, init
- [`include/`](include/) - public and internal headers used by kernel/userspace
- [`usr/`](usr/) - userspace components and libraries
- [`image/`](image/) - disk image assembly logic and root image content
- [`scripts/`](scripts/) - helper scripts for dependencies, toolchain, QEMU, GDB, formatting
- [`Documents/`](Documents/) - AsciiDoc source for project documentation

Important build files:

- [`SConstruct`](SConstruct) - root build entrypoint and global configuration
- [`kernel/SConscript`](kernel/SConscript) - kernel build rules
- [`usr/SConscript`](usr/SConscript) - userspace build rules
- [`image/SConscript`](image/SConscript) - image build rules

## Build Configuration Reference

Common SCons variables:

- `config`: `debug` (default) or `release`
- `arch`: `i686` (default), `x64`, `aarch64`
- `buildType`: `full` (default), `kernel`, `usr`, `image`
- `imageFS`: `fat12`, `fat16`, `fat32` (default), `ext2`
- `imageSize`: image size (for example `250m`)
- `outputFormat`: `img` (default) or `iso`
- `outputFile`: base name of output image

Examples:

```bash
scons config=release
scons arch=x64 config=release
scons buildType=kernel
scons outputFormat=iso
```

For full build and platform notes, read [`Documents/building.ad`](Documents/building.ad).

## Development and Contributing

Start with [`Documents/devel.ad`](Documents/devel.ad), which covers:

- Build/development workflow
- Git and patch submission expectations
- Coding style, naming, and commenting conventions
- Architecture separation and HAL strategy (no platform-specific assembly in common code)

If you plan a larger feature, check [`Documents/roadmap.ad`](Documents/roadmap.ad) to align with current priorities.

## Project Status

Valkyrie OS is actively developed. Roadmap highlights include:

- Core kernel infrastructure improvements
- Expanded syscall coverage
- Filesystem and device capability growth
- Longer-term advanced features (networking, extended process model)

Track current progress in [`Documents/roadmap.ad`](Documents/roadmap.ad).

## Licensing

Project licensing summary is in [`COPYING`](COPYING). Individual license texts are in [`LICENCES/`](LICENCES/).

## Support

If you find a bug or want to request a feature, open an issue on the GitHub repository and include:

- Target architecture/config
- Build command used
- Relevant logs or reproduction steps
