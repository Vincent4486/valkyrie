# Valkyrie OS

The Valkyrie Operating System is a small Unix-like OS for x86. It implements many Linux-like syscalls and GNU-compatible utilities. The tree also contains a custom JVM which is integrated into the kernel.

This repository targets low-level, cross-compiled builds for x86 (i686 and x64 variants). The build system uses `scons` and a small cross-toolchain produced by the included `scripts/base/toolchain.sh` helper.

## Documentation

- Source documentation for Valkyrie OS lives under `Documents/` in this repository.
- Build, toolchain, and usage instructions are maintained there instead of this README.
- Published documentation is available at: <a href="https://docs.vyang.org/valkyrie/"> My Site</a>

## Development

- Valkyrie OS is under active development. See the rest of the repository for kernel, drivers, filesystem, and userland sources.
- For documentation of development, see the `Documents` directory or <a href="https://docs.vyang.org/valkyrie/"> My Site</a>

## License

See the `COPYING` file for project licensing details.

## Support

If you find issues or want to request features, please open an issue on the project's GitHub repository.
