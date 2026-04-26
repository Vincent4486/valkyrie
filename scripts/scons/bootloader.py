# SPDX-License-Identifier: BSD-3-Clause

import copy
import os
import uuid

from SCons.Environment import Environment


Stage1PatchSignature = b'VLSP'
GptSignature = b'EFI PART'
BiosBootPartitionGuid = uuid.UUID('21686148-6449-6E6F-744E-656564454649')


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


def _DetectPartitionMap(ImagePath: str) -> str:
    with open(ImagePath, 'rb') as DiskFile:
        MbrSector = DiskFile.read(512)
        DiskFile.seek(512)
        Lba1Sector = DiskFile.read(512)

    if len(MbrSector) < 512:
        raise RuntimeError(f'Invalid disk image: short MBR sector in {ImagePath}')

    if MbrSector[510:512] != b'\x55\xaa':
        raise RuntimeError(f'MBR signature missing in disk image: {ImagePath}')

    if Lba1Sector[:8] == GptSignature:
        return 'gpt'

    return 'mbr'


def _GetMbrFirstPartitionStartLba(MbrSector: bytes) -> int:
    PartitionStarts = []
    for PartitionIndex in range(4):
        EntryOffset = 0x1BE + (PartitionIndex * 16)
        PartitionType = MbrSector[EntryOffset + 4]
        StartLba = int.from_bytes(MbrSector[EntryOffset + 8:EntryOffset + 12], byteorder='little')
        SectorCount = int.from_bytes(MbrSector[EntryOffset + 12:EntryOffset + 16], byteorder='little')
        if PartitionType == 0 or StartLba == 0 or SectorCount == 0:
            continue
        PartitionStarts.append(StartLba)

    if not PartitionStarts:
        return 0
    return min(PartitionStarts)


def _FindGptPartitionRangeByGuid(ImagePath: str, PartitionTypeGuid: uuid.UUID):
    with open(ImagePath, 'rb') as DiskFile:
        DiskFile.seek(512)
        Header = DiskFile.read(512)

        if Header[:8] != GptSignature:
            raise RuntimeError(f'GPT header not found at LBA1 in disk image: {ImagePath}')

        EntryStartLba = int.from_bytes(Header[72:80], byteorder='little')
        EntryCount = int.from_bytes(Header[80:84], byteorder='little')
        EntrySize = int.from_bytes(Header[84:88], byteorder='little')

        if EntryCount == 0:
            raise RuntimeError('GPT has zero partition entries')
        if EntrySize < 128:
            raise RuntimeError(f'GPT partition entry size too small: {EntrySize}')

        DiskFile.seek(EntryStartLba * 512)
        Entries = DiskFile.read(EntryCount * EntrySize)

    ExpectedType = PartitionTypeGuid.bytes_le
    for EntryIndex in range(EntryCount):
        Offset = EntryIndex * EntrySize
        Entry = Entries[Offset:Offset + EntrySize]
        if len(Entry) < 48:
            break

        TypeGuidBytes = Entry[0:16]
        if TypeGuidBytes == b'\x00' * 16:
            continue
        if TypeGuidBytes != ExpectedType:
            continue

        FirstLba = int.from_bytes(Entry[32:40], byteorder='little')
        LastLba = int.from_bytes(Entry[40:48], byteorder='little')
        if FirstLba == 0 or LastLba < FirstLba:
            continue
        return FirstLba, LastLba

    raise RuntimeError(
        'Required GPT partition type not found: '
        f'{str(PartitionTypeGuid).upper()}'
    )


def _PatchStage1LoadMetadata(Stage1Data: bytes, Stage2StartLba: int, Stage2SectorCount: int) -> bytes:
    MarkerOffset = Stage1Data.find(Stage1PatchSignature)
    if MarkerOffset < 0:
        raise RuntimeError(
            'Stage1 patch marker not found; expected embedded signature "VLSP"'
        )

    if Stage1Data.find(Stage1PatchSignature, MarkerOffset + 1) != -1:
        raise RuntimeError('Stage1 patch marker is not unique')

    EndOffset = MarkerOffset + 10
    if EndOffset > len(Stage1Data):
        raise RuntimeError('Stage1 patch marker is truncated')

    Patched = bytearray(Stage1Data)
    Patched[MarkerOffset + 4:MarkerOffset + 8] = Stage2StartLba.to_bytes(4, byteorder='little')
    Patched[MarkerOffset + 8:MarkerOffset + 10] = Stage2SectorCount.to_bytes(2, byteorder='little')
    return bytes(Patched)


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
    if Stage2DataSectors == 0:
        raise RuntimeError('Stage2 bootloader is empty')

    if Stage2DataSectors > 0xFFFF:
        raise RuntimeError(
            f"Stage2 sector count exceeds 16-bit limit: {Stage2DataSectors}"
        )

    with open(ImagePath, 'rb') as DiskFile:
        MbrSector = DiskFile.read(512)

    PartitionMap = _DetectPartitionMap(ImagePath)
    if PartitionMap == 'mbr':
        FirstPartitionStart = _GetMbrFirstPartitionStartLba(MbrSector)
        if FirstPartitionStart == 0:
            FirstPartitionStart = PartitionStartSector

        Stage2StartLba = 1
        ReservedSectors = FirstPartitionStart - Stage2StartLba
        if ReservedSectors <= 0:
            raise RuntimeError(
                f'MBR reserved area before first partition is too small: {ReservedSectors} sectors'
            )
    elif PartitionMap == 'gpt':
        Stage2StartLba, Stage2LastLba = _FindGptPartitionRangeByGuid(
            ImagePath,
            BiosBootPartitionGuid,
        )
        ReservedSectors = (Stage2LastLba - Stage2StartLba) + 1
    else:
        raise RuntimeError(f'Unsupported partition map: {PartitionMap}')

    if Stage2DataSectors > ReservedSectors:
        raise RuntimeError(
            f'Stage2 does not fit in reserved sectors ({ReservedSectors}): needs {Stage2DataSectors}'
        )

    Stage1Data = _PatchStage1LoadMetadata(Stage1Data, Stage2StartLba, Stage2DataSectors)

    Stage2PaddedSize = Stage2DataSectors * 512
    Stage2Payload = Stage2Data + b'\x00' * (Stage2PaddedSize - len(Stage2Data))

    print(
        '   INSTALL SYSTEM BOOTLOADER -> '
        f"{os.path.basename(Stage1Path)} "
        f'(map={PartitionMap}, stage2_lba={Stage2StartLba}, stage2_sectors={Stage2DataSectors})'
    )
    with open(ImagePath, 'r+b') as DiskFile:
        DiskFile.seek(0)
        DiskFile.write(Stage1Data)
        DiskFile.seek(Stage2StartLba * 512)
        DiskFile.write(Stage2Payload)