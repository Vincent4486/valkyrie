# SPDX-License-Identifier: BSD-3-Clause

import os
import shutil
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


def IsDarwin() -> bool:
    return os.uname().sysname == 'Darwin'


def GetHomebrewPrefixes() -> list:
    Prefixes = []
    EnvPrefix = os.environ.get('HOMEBREW_PREFIX')
    if EnvPrefix:
        Prefixes.append(EnvPrefix)
    Prefixes.extend(['/opt/homebrew', '/usr/local'])

    UniquePrefixes = []
    for Prefix in Prefixes:
        if Prefix not in UniquePrefixes:
            UniquePrefixes.append(Prefix)
    return UniquePrefixes


def GetExecutableSearchPath() -> str:
    Paths = os.environ.get('PATH', '').split(os.pathsep)
    for Prefix in GetHomebrewPrefixes():
        Paths.extend([
            os.path.join(Prefix, 'bin'),
            os.path.join(Prefix, 'sbin'),
            os.path.join(Prefix, 'opt', 'make', 'libexec', 'gnubin'),
            os.path.join(Prefix, 'opt', 'dosfstools', 'sbin'),
            os.path.join(Prefix, 'opt', 'e2fsprogs', 'bin'),
            os.path.join(Prefix, 'opt', 'e2fsprogs', 'sbin'),
        ])

    UniquePaths = []
    for PathEntry in Paths:
        if PathEntry and PathEntry not in UniquePaths:
            UniquePaths.append(PathEntry)
    return os.pathsep.join(UniquePaths)


def FindExecutable(Candidates: list, EnvOverride: str = None) -> str:
    SearchPath = GetExecutableSearchPath()
    if EnvOverride:
        Override = os.environ.get(EnvOverride)
        if Override:
            if os.path.isabs(Override) and os.path.exists(Override):
                return Override
            FoundOverride = shutil.which(Override, path=SearchPath)
            if FoundOverride:
                return FoundOverride

    for Candidate in Candidates:
        Found = shutil.which(Candidate, path=SearchPath)
        if Found:
            return Found

    return None


def RequireExecutable(
    Candidates: list,
    Purpose: str,
    EnvOverride: str = None,
    Suggestion: str = None,
) -> str:
    Found = FindExecutable(Candidates, EnvOverride)
    if Found:
        return Found

    Names = ', '.join(Candidates)
    Message = f"Missing required tool for {Purpose}: one of {Names}"
    if EnvOverride:
        Message += f" (or set {EnvOverride})"
    if Suggestion:
        Message += f". {Suggestion}"
    raise RuntimeError(Message)


def GetGrubMkimageCommand() -> str:
    return RequireExecutable(
        ['grub-mkimage', 'i686-elf-grub-mkimage', 'x86_64-elf-grub-mkimage'],
        'GRUB image generation',
        EnvOverride='GRUB_MKIMAGE',
        Suggestion='Install GRUB tools, or set GRUB_MKIMAGE to the executable path.',
    )


def GetGrubMkrescueCommand() -> str:
    return RequireExecutable(
        ['grub-mkrescue', 'i686-elf-grub-mkrescue', 'x86_64-elf-grub-mkrescue'],
        'GRUB rescue ISO generation',
        EnvOverride='GRUB_MKRESCUE',
        Suggestion='Install GRUB rescue tools, or set GRUB_MKRESCUE to the executable path.',
    )


def GetGrubPlatformDirectory(GrubTarget: str) -> str:
    Candidates = []
    Override = os.environ.get('GRUB_PLATFORM_DIR')
    if Override:
        Candidates.append(Override)

    Candidates.extend([
        os.path.join('/usr/lib/grub', GrubTarget),
        os.path.join('/usr/local/lib/grub', GrubTarget),
    ])

    for Prefix in GetHomebrewPrefixes():
        Candidates.extend([
            os.path.join(Prefix, 'opt', 'i686-elf-grub', 'lib', 'i686-elf', 'grub', GrubTarget),
            os.path.join(Prefix, 'opt', 'x86_64-elf-grub', 'lib', 'x86_64-elf', 'grub', GrubTarget),
        ])

    for Candidate in Candidates:
        if os.path.exists(os.path.join(Candidate, 'boot.img')):
            return Candidate

    raise RuntimeError(
        f"Could not find GRUB platform directory for {GrubTarget}. "
        "Install GRUB platform files, or set GRUB_PLATFORM_DIR to the directory "
        "containing boot.img."
    )


def PreflightImageTools(
    OutputFormat: str,
    Filesystem: str,
    BootSystem: str,
    BootType: str,
    Architecture: str,
):
    if OutputFormat == 'iso':
        if BootSystem == 'grub':
            GetGrubMkrescueCommand()
            RequireExecutable(
                ['xorriso'],
                'ISO generation',
                Suggestion='Install xorriso before building ISO images.',
            )
        return

    MacImageSuggestion = (
        "On macOS, raw .img builds require guestfish. Build an ISO with "
        "`scons ImageFormat=iso`, or build raw disk images in Linux or a Linux container."
        if IsDarwin()
        else 'Install guestfish/libguestfs before building raw disk images.'
    )

    RequireExecutable(['truncate'], 'raw disk image sizing')
    RequireExecutable(['dd'], 'raw disk image writing')
    RequireExecutable(
        ['guestfish'],
        'raw disk image partitioning and file injection',
        Suggestion=MacImageSuggestion,
    )
    RequireExecutable(
        [GetFilesystemConfig(Filesystem)['MakeFilesystemCommand']],
        f'{Filesystem} filesystem creation',
    )

    if BootSystem == 'grub':
        GetGrubMkimageCommand()
        if BootType == 'bios':
            GetGrubPlatformDirectory(GetGrubTarget(BootType, Architecture))


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
    MakeFilesystemName = FilesystemConfig['MakeFilesystemCommand']
    MakeFilesystemCommand = RequireExecutable(
        [MakeFilesystemName],
        f'{Filesystem} filesystem creation',
    )

    if MakeFilesystemName == 'mkfs.fat':
        RunCommand([
            MakeFilesystemCommand,
            *FilesystemConfig['MakeFilesystemArguments'],
            '-n',
            VolumeLabelName,
            PartitionPath,
        ])
    elif MakeFilesystemName == 'mkfs.ext2':
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
    RunCommand([
        GetGrubMkrescueCommand(),
        '-o',
        OutputIso,
        StagingDirectory,
        '--',
        '-volid',
        VolumeLabelName,
    ])


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
        GrubPrefix = GetGrubPrefix(PartitionMap)
        GrubMkimage = GetGrubMkimageCommand()

        print("   GRUB-MKIMAGE")
        if BootType == 'bios':
            GrubPath = GetGrubPlatformDirectory(GrubTarget)
            RunCommand([
                GrubMkimage,
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
                GrubMkimage,
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
