# SPDX-License-Identifier: BSD-3-Clause
"""
Architecture configuration for the Valkyrie OS build system.

This module defines architecture-specific settings including:
- Compiler flags
- Target triples
- Dynamic linker paths
- Assembly flags
"""

# Architecture configurations
ARCH_CONFIG = {
    'i686': {
        'target_triple': 'i686-linux-musl',
        'toolchain_prefix': 'i686-linux-musl-',
        'asm_format': 'elf32',
        'bits': 32,
        'ld_musl_name': 'ld-musl-i386.so.1',
        'define': 'I686',
        'asflags': ['-m32', '-g'],
        'ccflags': [],
        'linkflags': [],
        'qemu_system': 'qemu-system-i386',
        'qemu_machine': 'pc',
    },
    'x64': {
        'target_triple': 'x86_64-linux-musl',
        'toolchain_prefix': 'x86_64-linux-musl-',
        'asm_format': 'elf64',
        'bits': 64,
        'ld_musl_name': 'ld-musl-x86_64.so.1',
        'define': 'X64',
        'asflags': ['-m64', '-g'],
        'ccflags': [],
        'linkflags': [],
        'qemu_system': 'qemu-system-x86_64',
        'qemu_machine': 'pc',
    },
    'aarch64': {
        'target_triple': 'aarch64-linux-musl',
        'toolchain_prefix': 'aarch64-linux-musl-',
        'asm_format': 'elf64',
        'bits': 64,
        'ld_musl_name': 'ld-musl-aarch64.so.1',
        'define': 'AARCH64',
        'asflags': ['-g'],
        'ccflags': [],
        'linkflags': [],
        'qemu_system': 'qemu-system-aarch64',
        'qemu_machine': 'virt',
    },
}


def get_arch_config(arch: str) -> dict:
    """Get architecture configuration by name."""
    if arch not in ARCH_CONFIG:
        raise ValueError(f"Unsupported architecture: {arch}. "
                        f"Supported: {list(ARCH_CONFIG.keys())}")
    return ARCH_CONFIG[arch]


def get_supported_archs() -> list:
    """Get list of supported architectures."""
    return list(ARCH_CONFIG.keys())
