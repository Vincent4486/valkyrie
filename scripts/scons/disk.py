# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import textwrap

VolumeLabel = 'VALECIUM'

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

CommonGrubModules = [
    'biosdisk',
    'part_msdos',
    'normal',
    'configfile',
    'multiboot',
    'search',
    'search_label',
    'search_fs_uuid',
    'search_fs_file',
]


def GetFilesystemConfig(Filesystem: str) -> dict:
    if Filesystem not in FilesystemConfigurations:
        raise ValueError(f"Unsupported filesystem: {Filesystem}. "
                        f"Supported: {list(FilesystemConfigurations.keys())}")
    return FilesystemConfigurations[Filesystem]


def GetSupportedFilesystems() -> list:
    return list(FilesystemConfigurations.keys())


def GetPartitionTypeIdentifier(Filesystem: str) -> str:
    return GetFilesystemConfig(Filesystem)['PartitionTypeIdentifier']


def GetGrubModules(Filesystem: str) -> list:
    FilesystemModule = GetFilesystemConfig(Filesystem)['GrubFilesystemModule']
    Modules = [*CommonGrubModules, FilesystemModule]
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