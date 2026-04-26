# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import textwrap

from scripts.scons.bootloader import InstallSystemBootloader

VolumeLabel = 'VALECIUM'
EfiSystemPartitionGuid = 'C12A7328-F81F-11D2-BA4B-00A0C93EC93B'
LinuxFilesystemPartitionGuid = '0FC63DAF-8483-4772-8E79-3D69D8477DE4'
BiosBootPartitionGuid = '21686148-6449-6E6F-744E-656564454649'

FilesystemConfigurations = {
    'fat12': {
        'PartedType': 'fat12',
        'PartitionTypeIdentifier': '0x04',
        'MakeFilesystemCommand': 'mkfs.fat',
        'MakeFilesystemArguments': ['-F', '12'],
        'GrubFilesystemModule': 'fat',
        'SupportsSymlinks': False,
    },
    'fat16': {
        'PartedType': 'fat16',
        'PartitionTypeIdentifier': '0x06',
        'MakeFilesystemCommand': 'mkfs.fat',
        'MakeFilesystemArguments': ['-F', '16'],
        'GrubFilesystemModule': 'fat',
        'SupportsSymlinks': False,
    },
    'fat32': {
        'PartedType': 'fat32',
        'PartitionTypeIdentifier': '0x0c',
        'MakeFilesystemCommand': 'mkfs.fat',
        'MakeFilesystemArguments': ['-F', '32'],
        'GrubFilesystemModule': 'fat',
        'SupportsSymlinks': False,
    },
    'ext2': {
        'PartedType': 'ext2',
        'PartitionTypeIdentifier': '0x83',
        'MakeFilesystemCommand': 'mkfs.ext2',
        'MakeFilesystemArguments': [],
        'GrubFilesystemModule': 'ext2',
        'SupportsSymlinks': True,
    },
}

BootCommonGrubModules = [
    'normal',
    'configfile',
    'multiboot',
    'search',
    'search_label',
    'search_fs_uuid',
    'search_fs_file',
]

GrubPartitionMapModule = {
    'mbr': 'part_msdos',
    'gpt': 'part_gpt',
}

GrubTargetByBootAndArch = {
    ('bios', 'i686'): 'i386-pc',
    ('bios', 'x86_64'): 'i386-pc',
    ('efi', 'i686'): 'i386-efi',
    ('efi', 'x86_64'): 'x86_64-efi',
    ('efi', 'aarch64'): 'arm64-efi',
}

EfiDefaultBinaryByArch = {
    'i686': 'BOOTIA32.EFI',
    'x86_64': 'BOOTx86_64.EFI',
    'aarch64': 'BOOTAA64.EFI',
}


def GetFilesystemConfig(Filesystem: str) -> dict:
    if Filesystem not in FilesystemConfigurations:
        raise ValueError(f"Unsupported filesystem: {Filesystem}. "
                        f"Supported: {list(FilesystemConfigurations.keys())}")
    return FilesystemConfigurations[Filesystem]


def GetSupportedFilesystems() -> list:
    return list(FilesystemConfigurations.keys())


def GetSupportedPartitionMaps() -> list:
    return list(GrubPartitionMapModule.keys())


def GetPartitionTypeIdentifier(Filesystem: str) -> str:
    return GetFilesystemConfig(Filesystem)['PartitionTypeIdentifier']


def GetGrubTarget(BootType: str, Architecture: str) -> str:
    Key = (BootType, Architecture)
    if Key not in GrubTargetByBootAndArch:
        raise ValueError(
            f"Unsupported GRUB target for BootType={BootType}, BuildArch={Architecture}"
        )
    return GrubTargetByBootAndArch[Key]


def GetEfiDefaultBinaryName(Architecture: str) -> str:
    if Architecture not in EfiDefaultBinaryByArch:
        raise ValueError(f"Unsupported EFI architecture: {Architecture}")
    return EfiDefaultBinaryByArch[Architecture]


def GetGrubPrefix(PartitionMap: str) -> str:
    if PartitionMap == 'mbr':
        return '(hd0,msdos1)/boot/grub'
    if PartitionMap == 'gpt':
        return '(hd0,gpt1)/boot/grub'
    raise ValueError(f"Unsupported partition map: {PartitionMap}")


def GetGptPartitionTypeGuid(BootType: str) -> str:
    if BootType == 'efi':
        return EfiSystemPartitionGuid
    return LinuxFilesystemPartitionGuid


def GetGrubModules(Filesystem: str, BootType: str, PartitionMap: str) -> list:
    FilesystemModule = GetFilesystemConfig(Filesystem)['GrubFilesystemModule']
    if PartitionMap not in GrubPartitionMapModule:
        raise ValueError(f"Unsupported partition map: {PartitionMap}")

    Modules = [
        *BootCommonGrubModules,
        GrubPartitionMapModule[PartitionMap],
        FilesystemModule,
    ]
    if BootType == 'bios':
        Modules.insert(0, 'biosdisk')
    UniqueModules = []
    for Module in Modules:
        if Module not in UniqueModules:
            UniqueModules.append(Module)
    return UniqueModules


def RunCommand(Arguments: list, InputText: str = None):
    subprocess.run(
        Arguments,
        check=True,
        input=InputText,
        text=(InputText is not None),
    )


def FormatPartitionImage(PartitionPath: str, Filesystem: str, VolumeLabelName: str = VolumeLabel):
    FilesystemConfig = GetFilesystemConfig(Filesystem)
    MakeFilesystemCommand = FilesystemConfig['MakeFilesystemCommand']

    if MakeFilesystemCommand == 'mkfs.fat':
        RunCommand([
            MakeFilesystemCommand,
            *FilesystemConfig['MakeFilesystemArguments'],
            '-n',
            VolumeLabelName,
            PartitionPath,
        ])
    elif MakeFilesystemCommand == 'mkfs.ext2':
        RunCommand([MakeFilesystemCommand, '-L', VolumeLabelName, PartitionPath])
    else:
        raise ValueError(
            f'Unsupported mkfs command for filesystem {Filesystem}: {MakeFilesystemCommand}'
        )


def CreateBootableIso(StagingDirectory: str, OutputIso: str, VolumeLabelName: str = VolumeLabel):
    """Create a bootable ISO image using grub-mkrescue.

    The staging directory must already contain boot/grub/grub.cfg.

    Args:
        StagingDirectory: Directory tree to include in the ISO root
        OutputIso: Output ISO file path
        VolumeLabelName: ISO volume label
    """
    print("   GRUB-MKRESCUE")
    RunCommand(['grub-mkrescue', '-o', OutputIso, StagingDirectory, '--', '-volid', VolumeLabelName])


def CreateBootableDisk(
    Stage: str,
    ImagePath: str,
    Volume: str,
    Filesystem: str,
    PartMb: int,
    TotalMb: int,
    PartStartSector: int,
    PartitionTypeIdentifier: str,
    BootSystem: str,
    BootType: str,
    Architecture: str,
    PartitionMap: str,
    BootloaderComponents: dict,
):
    ImgDir = os.path.dirname(ImagePath) or '.'
    TmpPart = os.path.join(ImgDir, 'part.tmp')
    GrubCore = os.path.join(ImgDir, 'grub.core')
    BootHead = os.path.join(ImgDir, 'boot_head.img')
    BootHeadRequired = (BootSystem == 'grub' and BootType == 'bios')
    UseGptSystemBiosLayout = (
        BootSystem == 'system' and BootType == 'bios' and PartitionMap == 'gpt'
    )
    GeneratedEfiBinary = None
    RootPartitionIndex = 1

    if BootSystem == 'grub':
        GrubTarget = GetGrubTarget(BootType, Architecture)
        GrubPath = os.path.join('/usr/lib/grub', GrubTarget)
        GrubPrefix = GetGrubPrefix(PartitionMap)

        print("   GRUB-MKIMAGE")
        if BootType == 'bios':
            RunCommand([
                'grub-mkimage',
                '-O', GrubTarget,
                '-o', GrubCore,
                '-p', GrubPrefix,
                *GetGrubModules(Filesystem, BootType, PartitionMap),
            ])

            with open(os.path.join(GrubPath, 'boot.img'), 'rb') as BootImg, \
                 open(GrubCore, 'rb') as CoreImg, \
                 open(BootHead, 'wb') as OutImg:
                OutImg.write(BootImg.read())
                OutImg.write(CoreImg.read())
        elif BootType == 'efi':
            EfiDirectory = os.path.join(Stage, 'EFI', 'BOOT')
            os.makedirs(EfiDirectory, exist_ok=True)
            GeneratedEfiBinary = os.path.join(EfiDirectory, GetEfiDefaultBinaryName(Architecture))
            RunCommand([
                'grub-mkimage',
                '-O', GrubTarget,
                '-o', GeneratedEfiBinary,
                '-p', GrubPrefix,
                *GetGrubModules(Filesystem, BootType, PartitionMap),
            ])
        else:
            raise ValueError(f"Unsupported boot type: {BootType}")
    elif BootSystem != 'system':
        raise ValueError(f"Unsupported boot system: {BootSystem}")

    try:
        print(f"   CREATE PARTITION FILE {Filesystem}")
        RunCommand(['truncate', '-s', f'{PartMb}M', TmpPart])
        FormatPartitionImage(TmpPart, Filesystem, Volume)

        print(f"   CREATE {PartitionMap.upper()}")
        RunCommand(['truncate', '-s', f'{TotalMb}M', ImagePath])
        PartitionEndSector = '-1'
        if PartitionMap == 'gpt':
            PartitionEndSector = str((TotalMb * 2048) - 34)

        GuestfishPartitionCommands = [
            'run',
            f'part-init /dev/sda {PartitionMap}',
        ]
        if UseGptSystemBiosLayout:
            BiosBootStart = 34
            BiosBootEnd = PartStartSector - 1
            if BiosBootEnd < BiosBootStart:
                raise RuntimeError(
                    'Not enough room before root partition for GPT BIOS boot partition'
                )

            GuestfishPartitionCommands.extend([
                f'part-add /dev/sda p {BiosBootStart} {BiosBootEnd}',
                f'part-set-gpt-type /dev/sda 1 {BiosBootPartitionGuid}',
                f'part-add /dev/sda p {PartStartSector} {PartitionEndSector}',
                f'part-set-gpt-type /dev/sda 2 {GetGptPartitionTypeGuid(BootType)}',
            ])
            RootPartitionIndex = 2
        else:
            GuestfishPartitionCommands.append(
                f'part-add /dev/sda p {PartStartSector} {PartitionEndSector}'
            )
            if PartitionMap == 'mbr':
                GuestfishPartitionCommands.append(
                    f'part-set-mbr-id /dev/sda 1 {PartitionTypeIdentifier}'
                )
            elif PartitionMap == 'gpt':
                GuestfishPartitionCommands.append(
                    f'part-set-gpt-type /dev/sda 1 {GetGptPartitionTypeGuid(BootType)}'
                )
            else:
                raise ValueError(f"Unsupported partition map: {PartitionMap}")
        GuestfishPartitionCommands.extend(['quit', ''])
        RunCommand(
            ['guestfish', '-a', ImagePath],
            InputText='\n'.join(GuestfishPartitionCommands),
        )

        print("   SPLICE PARTITION")
        RunCommand([
            'dd',
            f'if={TmpPart}',
            f'of={ImagePath}',
            'bs=512',
            f'seek={PartStartSector}',
            'conv=notrunc',
            'status=none',
        ])

        if BootHeadRequired:
            print("   WRITE BOOTLOADER")
            RunCommand([
                'dd',
                f'if={BootHead}',
                f'of={ImagePath}',
                'bs=446',
                'count=1',
                'conv=notrunc',
                'status=none',
            ])
            RunCommand([
                'dd',
                f'if={BootHead}',
                f'of={ImagePath}',
                'bs=512',
                'skip=1',
                'seek=1',
                'conv=notrunc',
                'status=none',
            ])

            with open(ImagePath, 'r+b') as DiskFile:
                DiskFile.seek(92)
                DiskFile.write(b'\x01\x00\x00\x00')
        elif BootSystem == 'system':
            InstallSystemBootloader(
                ImagePath,
                BootType,
                BootloaderComponents,
                PartStartSector,
            )

        print(f"   INJECT FILES {Stage}")
        GuestfishCopy = '\n'.join([
            'run',
            f'mount /dev/sda{RootPartitionIndex} /',
            f'copy-in {Stage}/. /',
            'quit',
            '',
        ])
        RunCommand(['guestfish', '-a', ImagePath], InputText=GuestfishCopy)

    finally:
        for Tmp in (TmpPart, BootHead, GrubCore, GeneratedEfiBinary):
            if Tmp is not None and os.path.exists(Tmp):
                os.remove(Tmp)


def BuildGrubConfigContent(
    Config: str = 'release',
    KernelName: str = 'valeciumx',
    VolumeLabelName: str = VolumeLabel,
) -> str:
    """Generate GRUB configuration content based on build configuration.
    
    Args:
        Config: 'debug' or 'release'
        KernelName: kernel executable name under /boot
    
    Returns:
        GRUB configuration content as string
    """
    Timeout = '0' if Config == 'debug' else '10'
    
    return textwrap.dedent(f"""\
# Set a variable to prevent recursion loops
if [ -z "$configLoaded" ]; then
    set configLoaded=1

    # Force standard PC keyboard and console output
    terminal_input console
    terminal_output console

    set timeout_style=menu
    set timeout={Timeout}
    set default=0

    menuentry "Valecium OS" {{
        search --no-floppy --label {VolumeLabelName} --set=root
        multiboot /boot/{KernelName} root=LABEL={VolumeLabelName}
        boot
    }}

    menuentry "Reboot" {{
        reboot
    }}
fi
""")


def GenerateGrubConfig(
    GrubDirectory: str,
    OutputFormat: str = 'img',
    Config: str = 'release',
    KernelName: str = 'valeciumx',
    VolumeLabelName: str = VolumeLabel,
) -> str:
    """Generate grub.cfg for the given output format and build configuration.

    Args:
        GrubDirectory: Directory to write grub.cfg into
        OutputFormat: 'img' or 'iso' (currently equivalent)
        Config: 'debug' or 'release'
        KernelName: kernel executable name under /boot

    Returns:
        Path to the generated grub.cfg
    """
    _ = OutputFormat
    os.makedirs(GrubDirectory, exist_ok=True)
    ConfigPath = os.path.join(GrubDirectory, 'grub.cfg')
    Content = BuildGrubConfigContent(Config, KernelName, VolumeLabelName)
    with open(ConfigPath, 'w', encoding='utf-8') as FileHandle:
        FileHandle.write(Content)
    return ConfigPath
