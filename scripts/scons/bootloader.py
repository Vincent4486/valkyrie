# SPDX-License-Identifier: BSD-3-Clause

import copy
import os

from SCons.Environment import Environment

AllowedBootSetups = (
    # ISO is the only supported image format, so the boot setup is defined by
    # (architecture, boot type, bootloader).
    ('aarch64', 'efi', 'system'),
    ('aarch64', 'efi', 'grub'),
    ('x86_64', 'bios', 'system'),
    ('x86_64', 'bios', 'grub'),
    ('x86_64', 'efi', 'system'),
    ('x86_64', 'efi', 'grub'),
    ('i686', 'bios', 'system'),
    ('i686', 'bios', 'grub'),
)


BootloaderProfiles = {
    'bios': {
        'SupportedArchitectures': ['i686', 'x86_64'],
        'CompilerFlags': ['-DBOOTLOADER_BIOS=1'],
        'AssemblerFlags': ['-DBOOTLOADER_BIOS=1'],
    },
    'efi': {
        'SupportedArchitectures': ['x86_64', 'aarch64'],
        'CompilerFlags': ['-DBOOTLOADER_EFI=1'],
        'AssemblerFlags': ['-DBOOTLOADER_EFI=1'],
    },
}


def GetSupportedBootTypes() -> list:
    return list(BootloaderProfiles.keys())


def ValidateBootSetup(
    Architecture: str,
    BootType: str,
    Bootloader: str
) -> None:
    Config = (
        Architecture.lower(),
        BootType.lower(),
        Bootloader.lower()
    )
    if Config not in AllowedBootSetups:
        raise ValueError(
            f"Unsupported boot setup: "
            f"Architecture={Architecture}, "
            f"BootType={BootType}, "
            f"Bootloader={Bootloader}. "
        )


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


def PrepareElToritoBootImage(
    Stage1Path: str,
    Stage2Path: str,
) -> str:
    """Create a combined El Torito boot image from stage1 and stage2 binaries.

    Concatenates stage1 (padded to a full 512-byte sector) with stage2 to produce
    a flat binary suitable for El Torito "no emulation" boot.  The resulting image
    is placed alongside the stage1 file as ``<stage1_name>-eltorito.bin``.

    The El Torito Boot Info Table (patched by xorriso at ISO creation time) will
    occupy bytes 8--31 of the first sector, so stage1 must not place critical
    code or data in that region.

    Returns:
        Absolute path to the generated El Torito boot image.
    """
    if not os.path.exists(Stage1Path):
        raise FileNotFoundError(f"Stage1 bootloader file does not exist: {Stage1Path}")
    if not os.path.exists(Stage2Path):
        raise FileNotFoundError(f"Stage2 bootloader file does not exist: {Stage2Path}")

    with open(Stage1Path, 'rb') as Stage1File:
        Stage1Data = Stage1File.read()
    with open(Stage2Path, 'rb') as Stage2File:
        Stage2Data = Stage2File.read()

    # Pad stage1 to a full 512-byte sector so the Boot Info Table (bytes 8-31)
    # has a well-defined home and stage2 begins on a sector boundary.
    if len(Stage1Data) < 512:
        Stage1Data = Stage1Data + b'\x00' * (512 - len(Stage1Data))

    Combined = Stage1Data + Stage2Data

    OutputPath = os.path.join(
        os.path.dirname(Stage1Path),
        os.path.basename(Stage1Path).replace('.bin', '-eltorito.bin'),
    )
    with open(OutputPath, 'wb') as OutputFile:
        OutputFile.write(Combined)

    return os.path.abspath(OutputPath)
