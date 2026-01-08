# Valkyrie OS
The Valkyrie Operating System is an Unix-like operating system featureing most linux syscalls and GNU program compatibilities. The Operating system also contains a specially designed JVM which is a part of the kernel, making it more effecient to run Java programs. This Operating system is designed for the x86 archiecture, and other architectures like x64, aarch64 and risc-v are planed to be implemented in the future.

## Java Virtual Machine
The JVM for this operating system is custom, and it runs in kernel mode as a part of the kernel, making it not relying on syscalls which would improve the effeciency of the

## Install
To install Valkyrie OS, you can use the pre-built hard disk images from the release page of this repository. Alternatively, you can use the build tool to build the operating system locally. To build this operating system, please follow these steps. Building the Valkyrie OS requires the following dependencies:

### Prerequisites

Before building Valkyrie OS, ensure you have the following installed:

#### Always needed:
```
bison
flex
mpfr
gmp
mpc
gcc
nasm              # compiling assembly code
python3           # base python for scons
scons             # scons for build
python3-sh        # shell library
dosfstools        # mkfs.fat for creating disk
```

Linux is the only host operating system supported for building the Valkyrie OS, since building requires disk tools like ```parted``` to work.

#### Optional
```
perl           # autorun and other tools
clang-format   # formating code
LaTeX          # documentation
make           # build for docs
qemu           # enumalator for running OS
```
These tools are for extra utilities such as autorun for development and build documentation.

### Building from Source

1. **Install dependencies** (Ubuntu/Debian):
   ```bash
   sudo apt-get update
   sudo apt-get install -y bison flex libmpfr-dev libgmp-dev libmpc-dev gcc nasm scons python3 python3-sh dosfstools
   ```

2. **Build the cross-toolchain** (this may take a while):
   ```bash
   ./scripts/base/toolchain.sh toolchain
   ```

3. **Build the operating system**:
   ```bash
   scons
   ```

   To customize the build, you can use various options:
   - `config=debug` or `config=release` - Build configuration (default: debug)
   - `arch=i686` - Target architecture (currently only i686 supported)
   - `imageFS=fat12|fat16|fat32|ext2` - Filesystem type (default: fat32)
   - `imageSize=250m` - Image size with suffixes k/m/g (default: 250m)
   - `outputFile=name` - Output filename without extension (default: valkyrie_os)

   Example:
   ```bash
   scons config=release
   outputFile=valkyrie_disk
   ```

4. **Run the image** with QEMU:
   ```bash
   scons run
   ```

   Or manually:
   ```bash
   ./scripts/base/qemu.sh disk build/i686_debug/valkyrie_os.img
   ```

### Debugging

To debug the operating system with GDB:
```bash
scons debug
```

Or manually:
```bash
./scripts/base/gdb.sh disk build/i686_debug/valkyrie_os.img
```

## Development
Valkyrie OS is currently under development with the base system not finished. Please wait until the OS is released to find a fully working OS.

### Architecture

- **Bootloader (Stage 1)**: Loads Stage 2 from disk
- **Bootloader (Stage 2)**: Initializes hardware and loads the kernel
- **Kernel**: Manages system resources, drivers, and JVM execution
- **JVM**: Executes Java bytecode programs

### License

See the LICENSE file for details on the project's license.

### Support

For issues, questions, or suggestions, please open an issue on the GitHub repository.
