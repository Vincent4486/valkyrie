# SPDX-License-Identifier: BSD-3-Clause

ArchConfigurations = {
    'i686': {
        'TargetTriple': 'i686-linux-musl',
        'ToolchainPrefix': 'i686-linux-musl-',
        'AsmFormat': 'elf32',
        'Bits': 32,
        'LoaderMuslName': 'ld-musl-i386.so.1',
        'Define': 'I686',
        'AssemblyFlags': ['-m32', '-g'],
        'CompilerFlags': [],
        'LinkerFlags': [],
        'QemuSystem': 'qemu-system-i386',
        'QemuMachine': 'pc',
    },
    'x64': {
        'TargetTriple': 'x86_64-linux-musl',
        'ToolchainPrefix': 'x86_64-linux-musl-',
        'AsmFormat': 'elf64',
        'Bits': 64,
        'LoaderMuslName': 'ld-musl-x86_64.so.1',
        'Define': 'X64',
        'AssemblyFlags': ['-m64', '-g'],
        'CompilerFlags': [],
        'LinkerFlags': [],
        'QemuSystem': 'qemu-system-x86_64',
        'QemuMachine': 'pc',
    },
    'aarch64': {
        'TargetTriple': 'aarch64-linux-musl',
        'ToolchainPrefix': 'aarch64-linux-musl-',
        'AsmFormat': 'elf64',
        'Bits': 64,
        'LoaderMuslName': 'ld-musl-aarch64.so.1',
        'Define': 'AARCH64',
        'AssemblyFlags': ['-g'],
        'CompilerFlags': [],
        'LinkerFlags': [],
        'QemuSystem': 'qemu-system-aarch64',
        'QemuMachine': 'virt',
    },
}


def GetArchConfig(Architecture: str) -> dict:
    if Architecture not in ArchConfigurations:
        raise ValueError(f"Unsupported architecture: {Architecture}. "
                        f"Supported: {list(ArchConfigurations.keys())}")
    return ArchConfigurations[Architecture]


def GetSupportedArchitectures() -> list:
    return list(ArchConfigurations.keys())
