# Valkyrie OS

The Valkyrie Operating System is a small Unix-like OS for x86. It implements many Linux-like syscalls and GNU-compatible utilities. The tree also contains a custom JVM which is integrated into the kernel.

This repository targets low-level, cross-compiled builds for x86 (currently i686). The build system now uses a C build driver (`tools/builder/build.c`) and a small cross-toolchain produced by the included `scripts/base/toolchain.py` helper.

**Supported host environments**
- Primary: Linux distributions (Debian/Ubuntu, Fedora, Arch, openSUSE, Alpine). The repository includes `scripts/base/dependencies.sh` which detects the distro and installs the required packages.
- macOS: partial support for building the cross-toolchain (toolchain script includes Darwin-specific flags). The overall image build and tooling are intended for Linux hosts.

Required build artifacts created by the toolchain script:
- binutils: 2.45
- gcc: 15.2.0
- musl (for sysroot): 1.2.5

Prerequisites
 - On Linux, prefer using `scripts/base/dependencies.sh` to install the common packages for your distribution. The script will detect your distro and run the appropriate package manager. Example packages (by name) the script installs:
  - Debian/Ubuntu: `libmpfr-dev libgmp-dev libmpc-dev gcc python3 python3-sh dosfstools`
  - Fedora: `mpfr-devel gmp-devel libmpc-devel gcc python3 python3-sh dosfstools`
  - Arch: `mpfr gmp mpc gcc python python-sh dosfstools`
   - openSUSE / Alpine: similar package names (see `scripts/base/dependencies.sh`).

 - On macOS (if you wish to run the toolchain script), install the GMP/MPFR/MPC libraries with Homebrew so the toolchain script can find them:
   ```bash
   brew install gmp mpfr libmpc wget make automake autoconf pkg-config
   ```

Note: `scripts/base/dependencies.sh` is designed for Linux package managers and will exit on unknown systems. On macOS you must install the prerequisites manually.

Build steps

1) (Optional) Install distribution packages on Linux:

```bash
sudo ./scripts/base/dependencies.sh
```

The script will show the package manager command and prompt for confirmation.

2) Build a cross-toolchain (recommended):

```bash
make toolchain
```

This produces a `toolchain/` directory with target subdirectories such as `toolchain/i686-elf` containing `bin/` and `lib/` used by the build.

Advanced/manual: you can run the helper script directly:

```bash
python3 ./scripts/base/toolchain.py toolchain/ -t i686-linux-musl
```

3) Build the project:

```bash
make build
```

Common build options are read from `.config` and can be changed via `tools/builder/config`:
- `config=debug|release` - build configuration (default: `debug`)
- `arch=i686` - target architecture (default: `i686`)
- `imageFS=fat12|fat16|fat32|ext2` - filesystem for generated image (default: `fat32`)
- `imageSize=250m` - image size (supports `k/m/g` suffixes)
- `outputFile=<name>` - base name for output image (default: `image`)
- `buildType=full|kernel|usr|image` - what to build (default: `full`)

Example direct invocation:

```bash
./tools/builder/build --build
./tools/builder/build --set config=release --set buildType=kernel --build
```

4) Run and debug

The C build driver exposes common targets (which invoke scripts in `scripts/base/`):

- `make run` — run the built image under QEMU
- `make debug` — start a GDB-enabled debug session
- `make bochs` — run with Bochs
- `make toolchain` — (re)build the cross-toolchain

Or call the helper scripts directly, for example:

```bash
./scripts/base/qemu.sh disk build/i686_debug/valkyrix
./scripts/base/gdb.sh disk build/i686_debug/valkyrix
```

Notes and tips
- The build is designed for Linux hosts; while the toolchain script contains macOS-specific options, full image creation and tools like `mkfs` and `parted` are Linux-focused.
- Building the toolchain can take significant time and requires a number of build dependencies (autoconf/automake, make, wget, and the GMP/MPFR/MPC dev packages).
- If you prefer not to build a toolchain locally, providing a prebuilt cross-toolchain in `toolchain/<target>` works: SCons will use `--toolchain` path when provided.

Development

Valkyrie OS is under active development. See the rest of the repository for kernel, drivers, filesystem, and userland sources.

License

See the LICENSE file for project licensing details.

Support

If you find issues or want to request features, please open an issue on the project's GitHub repository.
