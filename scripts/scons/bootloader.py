# SPDX-License-Identifier: BSD-3-Clause

import copy
import os

from SCons.Environment import Environment


BootloaderProfiles = {
    'bios': {
        'SupportedArchitectures': ['i686', 'x86_64'],
        'CompilerFlags': ['-DBOOTLOADER_BIOS=1'],
        'AssemblerFlags': ['-DBOOTLOADER_BIOS=1'],
    },
    'efi': {
        'SupportedArchitectures': ['i686', 'x86_64', 'aarch64'],
        'CompilerFlags': ['-DBOOTLOADER_EFI=1'],
        'AssemblerFlags': ['-DBOOTLOADER_EFI=1'],
    },
}


def GetBootloaderBuildConfig(BootType: str, Architecture: str) -> dict:
    if BootType not in BootloaderProfiles:
        raise ValueError(
            f"Unsupported boot type: {BootType}. Supported: {list(BootloaderProfiles.keys())}"
        )

    config = copy.deepcopy(BootloaderProfiles[BootType])
    supported_architectures = config.get('SupportedArchitectures', [])
    if supported_architectures and Architecture not in supported_architectures:
        raise ValueError(
            f"Unsupported architecture {Architecture} for boot type {BootType}. "
            f"Supported: {supported_architectures}"
        )

    config['BootType'] = BootType
    config['Architecture'] = Architecture
    config['OutputName'] = f'bootloader-{BootType}-{Architecture}'

    return config


def ConfigureBootloaderEnvironment(
    env: Environment,
    source_path: str,
    architecture_path: str,
    architecture_config: dict,
    bootloader_config: dict,
):
    env.Append(
        ASFLAGS=architecture_config.get('AssemblyFlags', []),
        CCFLAGS=architecture_config.get('CompilerFlags', []),
        LINKFLAGS=architecture_config.get('LinkerFlags', []),
    )

    env.Append(
        CCFLAGS=[
            '-ffreestanding',
            '-fno-stack-protector',
            '-fno-builtin',
            '-Wall',
            '-Wextra',
        ],
        CPATH=[source_path, os.path.join(source_path, 'common'), '#include'],
        CPPPATH=[source_path, os.path.join(source_path, 'common'), '#include'],
        ASFLAGS=[
            '-I', source_path,
            '-I', os.path.join(source_path, 'common'),
            '-I', architecture_path,
            '-g',
            '-Wa,--noexecstack',
        ],
    )

    env.Append(CCFLAGS=bootloader_config.get('CompilerFlags', []))
    env.Append(ASFLAGS=bootloader_config.get('AssemblerFlags', []))