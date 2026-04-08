# SPDX-License-Identifier: BSD-3-Clause

import os
import textwrap
import shutil

import sh

VOLUME_LABEL = 'VALECIUM'

FS_CONFIG = {
    'fat12': {
        'parted_type': 'fat12',
        'part_type_id': '0x04',
        'mkfs_command': 'mkfs.fat',
        'mkfs_args': ['-F', '12'],
        'grub_fs_module': 'fat',
        'supports_symlinks': False,
    },
    'fat16': {
        'parted_type': 'fat16',
        'part_type_id': '0x06',
        'mkfs_command': 'mkfs.fat',
        'mkfs_args': ['-F', '16'],
        'grub_fs_module': 'fat',
        'supports_symlinks': False,
    },
    'fat32': {
        'parted_type': 'fat32',
        'part_type_id': '0x0c',
        'mkfs_command': 'mkfs.fat',
        'mkfs_args': ['-F', '32'],
        'grub_fs_module': 'fat',
        'supports_symlinks': False,
    },
    'ext2': {
        'parted_type': 'ext2',
        'part_type_id': '0x83',
        'mkfs_command': 'mkfs.ext2',
        'mkfs_args': [],
        'grub_fs_module': 'ext2',
        'supports_symlinks': True,
    },
}

COMMON_GRUB_MODULES = [
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


def get_fs_config(fs: str) -> dict:
    """Get filesystem configuration by name."""
    if fs not in FS_CONFIG:
        raise ValueError(f"Unsupported filesystem: {fs}. "
                        f"Supported: {list(FS_CONFIG.keys())}")
    return FS_CONFIG[fs]


def get_supported_filesystems() -> list:
    """Get list of supported filesystems."""
    return list(FS_CONFIG.keys())


def get_partition_type_id(fs: str) -> str:
    """Get MBR partition type ID for a filesystem."""
    return get_fs_config(fs)['part_type_id']


def get_grub_modules(fs: str) -> list:
    """Get GRUB modules required for filesystem + common runtime commands."""
    fs_module = get_fs_config(fs)['grub_fs_module']
    modules = [*COMMON_GRUB_MODULES, fs_module]
    unique = []
    for module in modules:
        if module not in unique:
            unique.append(module)
    return unique


def format_partition_image(partition_path: str, filesystem: str, volume_label: str = VOLUME_LABEL):
    """Format a standalone partition image file."""
    fs_config = get_fs_config(filesystem)
    mkfs_command = fs_config['mkfs_command']

    if mkfs_command == 'mkfs.fat':
        sh.Command(mkfs_command)(*fs_config['mkfs_args'], '-n', volume_label, partition_path)
    elif mkfs_command == 'mkfs.ext2':
        sh.Command(mkfs_command)('-L', volume_label, partition_path)
    else:
        raise ValueError(f'Unsupported mkfs command for filesystem {filesystem}: {mkfs_command}')


def copy_toolchain_runtime_to_staging(staging_root: str, target_sysroot: str, arch_config: dict):
    """Copy required shared C runtime libraries to staging without root permissions."""
    ld_name = arch_config['ld_musl_name']

    if not target_sysroot:
        print('   WARNING: compiler sysroot is empty; skipping runtime copy')
        return

    sysroot = os.path.join(target_sysroot, 'usr')
    if not os.path.exists(sysroot):
        print(f"   WARNING: compiler sysroot not found at {sysroot}")
        return

    print("   CP toolchain C shared runtime")

    sysroot_lib = os.path.join(sysroot, 'lib')
    target_lib = os.path.join(staging_root, 'lib')

    if os.path.exists(sysroot_lib):
        os.makedirs(target_lib, exist_ok=True)

        libc_src = os.path.join(sysroot_lib, 'libc.so')
        if os.path.exists(libc_src):
            shutil.copy2(libc_src, target_lib)

        lib_libc = os.path.join(target_lib, 'libc.so')
        lib_ld = os.path.join(target_lib, ld_name)
        if os.path.exists(lib_libc):
            print(f"   CP /lib/{ld_name}")
            shutil.copy2(lib_libc, lib_ld)


def create_bootable_iso(staging_dir: str, output_iso: str, volume_label: str = 'VALECIUM'):
    """Create a bootable ISO image using grub-mkrescue.

    The staging directory must already contain boot/grub/grub.cfg.

    Args:
        staging_dir: Directory tree to include in the ISO root
        output_iso: Output ISO file path
        volume_label: ISO volume label
    """
    print("   GRUB-MKRESCUE")
    grub_mkrescue = sh.Command('grub-mkrescue')
    grub_mkrescue('-o', output_iso, staging_dir, '--', '-volid', volume_label)


def _get_grub_config(config: str = 'release', kernel_name: str = 'valeciumx', volume_label: str = VOLUME_LABEL) -> str:
    """Generate GRUB configuration content based on build configuration.
    
    Args:
        config: 'debug' or 'release'
        kernel_name: kernel executable name under /boot
    
    Returns:
        GRUB configuration content as string
    """
    timeout = '0' if config == 'debug' else '10'
    
    return textwrap.dedent(f"""\
# Set a variable to prevent recursion loops
if [ -z "$config_loaded" ]; then
    set config_loaded=1

    # Force standard PC keyboard and console output
    terminal_input console
    terminal_output console

    set timeout_style=menu
    set timeout={timeout}
    set default=0

    menuentry "Valecium OS" {{
        search --no-floppy --label {volume_label} --set=root
        multiboot /boot/{kernel_name} root=LABEL={volume_label}
        boot
    }}

    menuentry "Reboot" {{
        reboot
    }}
fi
""")


def generate_grub_config(
    grub_dir: str,
    output_format: str = 'img',
    config: str = 'release',
    kernel_name: str = 'valeciumx',
    volume_label: str = VOLUME_LABEL,
) -> str:
    """Generate grub.cfg for the given output format and build configuration.

    Args:
        grub_dir: Directory to write grub.cfg into
        output_format: 'img' or 'iso' (currently equivalent)
        config: 'debug' or 'release'
        kernel_name: kernel executable name under /boot

    Returns:
        Path to the generated grub.cfg
    """
    os.makedirs(grub_dir, exist_ok=True)
    cfg_path = os.path.join(grub_dir, 'grub.cfg')
    content = _get_grub_config(config, kernel_name, volume_label)
    with open(cfg_path, 'w', encoding='utf-8') as f:
        f.write(content)
    return cfg_path