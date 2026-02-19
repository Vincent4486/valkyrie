# SPDX-License-Identifier: BSD-3-Clause
"""
Disk image creation utilities for the Valkyrie OS build system.

This module handles:
- Image file generation
- Partition table creation
- Filesystem formatting
- Mounting/unmounting
- File copying to images
"""

import os
import shutil
import subprocess
import sys
import textwrap
import time

import sh

SECTOR_SIZE = 512

# Filesystem configurations
FS_CONFIG = {
    'fat12': {
        'parted_type': 'fat12',
        'mkfs_cmd': 'mkfs.fat',
        'fat_size': '12',
        'reserved_extra': 1,
        'supports_symlinks': False,
    },
    'fat16': {
        'parted_type': 'fat16',
        'mkfs_cmd': 'mkfs.fat',
        'fat_size': '16',
        'reserved_extra': 1,
        'supports_symlinks': False,
    },
    'fat32': {
        'parted_type': 'fat32',
        'mkfs_cmd': 'mkfs.fat',
        'fat_size': '32',
        'reserved_extra': 2,
        'supports_symlinks': False,
    },
    'ext2': {
        'parted_type': 'ext2',
        'mkfs_cmd': 'mkfs.ext2',
        'supports_symlinks': True,
    },
}


def get_fs_config(fs: str) -> dict:
    """Get filesystem configuration by name."""
    if fs not in FS_CONFIG:
        raise ValueError(f"Unsupported filesystem: {fs}. "
                        f"Supported: {list(FS_CONFIG.keys())}")
    return FS_CONFIG[fs]


def get_supported_filesystems() -> list:
    """Get list of supported filesystems."""
    return list(FS_CONFIG.keys())


class DiskImage:
    """Handles disk image creation and population."""
    
    def __init__(self, image_path: str, size_bytes: int, sector_size: int = 512):
        self.image_path = image_path
        self.size_bytes = size_bytes
        self.sector_size = sector_size
        self.size_sectors = (size_bytes + sector_size - 1) // sector_size
        
        self._mount_dir = None
        self._device = None
    
    def create(self):
        """Create the disk image file."""
        with open(self.image_path, 'wb') as f:
            f.write(bytes(self.size_sectors * self.sector_size))
    
    def create_partition_table(self, offset: int = 2048, filesystem: str = 'fat32'):
        """Create partition table on the image.
        
        Args:
            offset: Partition start offset in sectors
            filesystem: Filesystem type for partition ID
        """
        fs_config = get_fs_config(filesystem)
        
        # Linux uses parted
        sh.parted('-s', self.image_path, 'mklabel', 'msdos')
        sh.parted('-s', self.image_path, 'mkpart', 'primary',
                 fs_config['parted_type'], f'{offset}s', '100%')
        sh.parted('-s', self.image_path, 'set', '1', 'boot', 'on')
    
    def format_partition(self, filesystem: str, offset: int = 0, 
                        reserved_sectors: int = 0, label: str = 'VALKYRIE'):
        """Format the partition with the specified filesystem.
        
        Args:
            filesystem: Filesystem type (fat12, fat16, fat32, ext2)
            offset: Partition offset in sectors
            reserved_sectors: Additional reserved sectors
            label: Volume label
        """
        if filesystem.startswith('fat'):
            fat_reserved = reserved_sectors + 1
            if filesystem == 'fat32':
                fat_reserved += 1
            
            mkfs_fat = sh.Command('mkfs.fat')
            mkfs_fat(self.image_path,
                    F=filesystem[3:],      # fat size (12, 16, 32)
                    n=label,
                    R=fat_reserved,
                    offset=offset)
        elif filesystem == 'ext2':
            mkfs_ext2 = sh.Command('mkfs.ext2')
            mkfs_ext2(self.image_path,
                     L=label,
                     E=f'offset={offset * self.sector_size}')
        else:
            raise ValueError(f'Unsupported filesystem: {filesystem}')
    
    def mount(self, mount_dir: str):
        """Mount the image to the specified directory."""
        self._mount_dir = mount_dir
        os.makedirs(mount_dir, exist_ok=True)

        # Setup loop device (Linux)
        self._device = sh.sudo.losetup('-fP', '--show', self.image_path).strip()

        sh.sudo.partprobe(self._device)
        partition = f'{self._device}p1'
        
        # Wait for partition to appear
        self._wait_for_partition(partition)
        
        # Store device path for unmount
        device_file = mount_dir + '.device'
        with open(device_file, 'w') as f:
            f.write(self._device)
        
        print(f"   MOUNT {partition} {mount_dir}")
        sh.sudo.mount(partition, mount_dir)
    
    def _wait_for_partition(self, partition: str, max_retries: int = 10):
        """Wait for loop partition to become available."""
        for _ in range(max_retries):
            result = subprocess.run(['ls', '-la', partition], 
                                   capture_output=True, timeout=1)
            if result.returncode == 0:
                return
            time.sleep(0.5)

        subprocess.run(['losetup', '-l'], check=False)
        raise RuntimeError(f"Loop partition {partition} not found")
    
    def unmount(self, mount_dir: str = None):
        """Unmount and detach the loop device.
        
        Args:
            mount_dir: Mount directory (optional if mount() was called)
        """
        mount_dir = mount_dir or self._mount_dir
        if not mount_dir:
            return
            
        time.sleep(2)  # Allow pending operations to complete
        
        device_file = mount_dir + '.device'
        
        sh.sudo.umount(mount_dir)
        
        with open(device_file, 'r') as f:
            device = f.read().strip()
        
        sh.sudo.losetup('-d', device)
        os.remove(device_file)


def get_current_user() -> str:
    """Get the current user name."""
    try:
        return os.getlogin()
    except:
        return os.environ.get('USER', 'root')


def copy_c_library(mount_dir: str, toolchain_prefix: str, target_triple: str, arch_config: dict):
    """Copy C library and runtime files to the disk image.
    
    Args:
        mount_dir: Mounted filesystem root directory
        toolchain_prefix: Path to the toolchain directory
        target_triple: Target triple (e.g., i686-linux-musl)
        arch_config: Architecture configuration dict from get_arch_config()
    """
    ld_name = arch_config['ld_musl_name']
    
    sysroot = os.path.join(toolchain_prefix, target_triple, 'sysroot', 'usr')
    if not os.path.exists(sysroot):
        print(f"   WARNING: toolchain sysroot not found at {sysroot}")
        return
    
    print("   CP toolchain C libraries")
    
    sysroot_lib = os.path.join(sysroot, 'lib')
    target_lib = os.path.join(mount_dir, 'lib')
    
    if os.path.exists(sysroot_lib):
        sh.sudo.mkdir('-p', target_lib)
        
        # Copy libc.so
        libc_src = os.path.join(sysroot_lib, 'libc.so')
        if os.path.exists(libc_src):
            sh.sudo.cp(libc_src, target_lib)
        
        # Copy startup/runtime files
        for crt_file in ['Scrt1.o', 'crt1.o', 'crti.o', 'crtn.o']:
            crt_path = os.path.join(sysroot_lib, crt_file)
            if os.path.exists(crt_path):
                sh.sudo.cp(crt_path, target_lib)
        
        # Create dynamic linker (copy since FAT doesn't support symlinks)
        lib_libc = os.path.join(target_lib, 'libc.so')
        lib_ld = os.path.join(target_lib, ld_name)
        print(f"   CP /lib/{ld_name}")
        sh.sudo.cp(lib_libc, lib_ld)


def create_bootable_iso(staging_dir: str, output_iso: str, volume_label: str = 'VALKYRIE'):
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


def install_grub(mount_dir: str):
    """Install GRUB bootloader to the mounted image."""
    device_file = mount_dir + '.device'
    if not os.path.exists(device_file):
        raise RuntimeError(f"Device file {device_file} not found")
    
    with open(device_file, 'r') as f:
        device = f.read().strip()
    
    bootdir = os.path.join(mount_dir, 'boot')
    sh.sudo.mkdir('-p', bootdir)
    
    print("   GRUB-INSTALL")
    sh.sudo('grub-install', '--target=i386-pc', 
            f'--boot-directory={bootdir}', '--recheck', device)


_GRUB_CFG_HD = textwrap.dedent("""\
set timeout=0
set default=0

menuentry "Valkyrie OS" {
    search --no-floppy --file /boot/valkyrix --set=root || search --no-floppy --label VALKYRIE --set=root
    multiboot /boot/valkyrix
    boot
}
""")

_GRUB_CFG_ISO = textwrap.dedent("""\
set timeout=0
set default=0

menuentry "Valkyrie OS" {
    multiboot /boot/valkyrix
    boot
}
""")


def generate_grub_config(grub_dir: str, output_format: str = 'hd') -> str:
    """Generate grub.cfg for the given output format.

    Args:
        grub_dir: Directory to write grub.cfg into
        output_format: 'hd' or 'iso'

    Returns:
        Path to the generated grub.cfg
    """
    os.makedirs(grub_dir, exist_ok=True)
    cfg_path = os.path.join(grub_dir, 'grub.cfg')
    content = _GRUB_CFG_ISO if output_format == 'iso' else _GRUB_CFG_HD
    with open(cfg_path, 'w', encoding='utf-8') as f:
        f.write(content)
    return cfg_path