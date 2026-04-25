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

BootSystemProfiles = {
    'grub': {
        'BuildSystemBootloader': False,
    },
    'system': {
        'BuildSystemBootloader': True,
    },
}


def GetSupportedBootTypes() -> list:
    return list(BootloaderProfiles.keys())


def GetSupportedBootSystems() -> list:
    return list(BootSystemProfiles.keys())


def ShouldBuildSystemBootloader(BootSystem: str) -> bool:
    if BootSystem not in BootSystemProfiles:
        raise ValueError(
            f"Unsupported boot system: {BootSystem}. Supported: {list(BootSystemProfiles.keys())}"
        )

    return bool(BootSystemProfiles[BootSystem]['BuildSystemBootloader'])


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


def _ResolveBootloaderComponentPath(component) -> str:
    if component is None:
        return ''

    if hasattr(component, 'get_abspath'):
        return component.get_abspath()

    return os.path.abspath(str(component))


def InstallSystemBootloader(
    ImagePath: str,
    BootType: str,
    BootloaderComponents: dict,
    PartitionStartSector: int,
):
    if BootType != 'bios':
        raise ValueError(
            f"BootSystem 'system' currently supports only BootType='bios', got: {BootType}"
        )

    Stage1Path = _ResolveBootloaderComponentPath(BootloaderComponents.get('Stage1'))
    Stage2Path = _ResolveBootloaderComponentPath(BootloaderComponents.get('Stage2'))
    if not Stage1Path:
        raise RuntimeError("Missing Stage1 bootloader component for system boot install")
    if not Stage2Path:
        raise RuntimeError("Missing Stage2 bootloader component for system boot install")
    if not os.path.exists(Stage1Path):
        raise FileNotFoundError(f"Stage1 bootloader file does not exist: {Stage1Path}")
    if not os.path.exists(Stage2Path):
        raise FileNotFoundError(f"Stage2 bootloader file does not exist: {Stage2Path}")

    with open(Stage1Path, 'rb') as Stage1File:
        Stage1Data = Stage1File.read()
    with open(Stage2Path, 'rb') as Stage2File:
        Stage2Data = Stage2File.read()

    if len(Stage1Data) > 0x1BE:
        raise RuntimeError(
            f"BIOS stage1 exceeds boot code area (446 bytes): {len(Stage1Data)} bytes"
        )

    Stage2DataSectors = (len(Stage2Data) + 511) // 512
    TotalStage2Sectors = 1 + Stage2DataSectors

    if TotalStage2Sectors > 0xFFFF:
        raise RuntimeError(
            f"Stage2 sector count exceeds 16-bit limit: {TotalStage2Sectors}"
        )

    ReservedSectors = PartitionStartSector - 1
    if TotalStage2Sectors > ReservedSectors:
        raise RuntimeError(
            f"Stage2 does not fit in reserved sectors ({ReservedSectors}): needs {TotalStage2Sectors}"
        )

    Stage2Header = bytearray(512)
    Stage2Header[0:4] = (0x32534C56).to_bytes(4, byteorder='little')
    Stage2Header[4:6] = TotalStage2Sectors.to_bytes(2, byteorder='little')

    Stage2PaddedSize = Stage2DataSectors * 512
    Stage2Payload = Stage2Data + b'\x00' * (Stage2PaddedSize - len(Stage2Data))

    print(f"   INSTALL SYSTEM BOOTLOADER -> {os.path.basename(Stage1Path)}")
    with open(ImagePath, 'r+b') as DiskFile:
        DiskFile.seek(0)
        DiskFile.write(Stage1Data)
        DiskFile.seek(512)
        DiskFile.write(Stage2Header)
        DiskFile.write(Stage2Payload)