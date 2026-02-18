# SPDX-License-Identifier: BSD-3-Clause
"""
Disk/ISO image creation utilities for the Valkyrie OS build system.

HD images are created and populated with libguestfs guestfish.
ISO images are created with xorriso.
"""

import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

SECTOR_SIZE = 512

# Filesystem configurations
FS_CONFIG = {
    'fat12': {
        'guestfish_type': 'vfat',
    },
    'fat16': {
        'guestfish_type': 'vfat',
    },
    'fat32': {
        'guestfish_type': 'vfat',
    },
    'ext2': {
        'guestfish_type': 'ext2',
    },
}


def _run_command(cmd: list[str]):
    """Run a command and raise with detailed output on failure."""
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(cmd)}\n"
            f"STDOUT:\n{result.stdout}\n"
            f"STDERR:\n{result.stderr}"
        )
    return result.stdout


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
    """Handles HD disk image creation and population using guestfish."""

    def __init__(self, image_path: str, size_bytes: int, sector_size: int = 512):
        self.image_path = image_path
        self.size_bytes = int(size_bytes)
        self.sector_size = sector_size
        self.size_sectors = (self.size_bytes + sector_size - 1) // sector_size

    def create(self):
        """Create the disk image file."""
        with open(self.image_path, 'wb') as f:
            f.truncate(self.size_sectors * self.sector_size)

    def _run_guestfish(self, commands: list[str]):
        """Run a batch of guestfish commands against this image."""
        cmd = ['guestfish', '--rw', '-a', self.image_path]
        for line in commands:
            cmd.extend(['--cmd', line])

        _run_command(cmd)

    def create_partition_table(self, offset: int = 2048, filesystem: str = 'fat32'):
        """Create an MBR partition table with a single bootable partition."""
        get_fs_config(filesystem)

        start = int(offset)
        end = int(self.size_sectors - 1)
        if end <= start:
            raise ValueError('Disk image is too small for requested partition offset')

        self._run_guestfish([
            'run',
            'part-init /dev/sda mbr',
            f'part-add /dev/sda p {start} {end}',
            'part-set-bootable /dev/sda 1 true',
        ])

    def format_partition(self, filesystem: str, offset: int = 0,
                         reserved_sectors: int = 0, label: str = 'VALKYRIE'):
        """Format the first partition."""
        del offset
        del reserved_sectors

        fs_config = get_fs_config(filesystem)
        fs_type = fs_config['guestfish_type']

        self._run_guestfish([
            'run',
            f'mkfs {fs_type} /dev/sda1',
        ])

        # FAT labels may fail for overlong labels; keep build resilient.
        try:
            self._run_guestfish([
                'run',
                f'set-label /dev/sda1 {label}',
            ])
        except RuntimeError:
            pass

    def populate_from_directory(self, source_dir: str):
        """Copy the staged filesystem tree into partition 1."""
        source_dir = os.path.abspath(source_dir)
        if not os.path.isdir(source_dir):
            raise RuntimeError(f'Source directory not found: {source_dir}')

        with tempfile.NamedTemporaryFile(suffix='.tar', delete=False) as tar_file:
            tar_path = tar_file.name

        try:
            with tarfile.open(tar_path, 'w') as archive:
                for entry in sorted(os.listdir(source_dir)):
                    archive.add(
                        os.path.join(source_dir, entry),
                        arcname=entry,
                        recursive=True,
                    )

            self._run_guestfish([
                'run',
                'mount /dev/sda1 /',
                f'tar-in {tar_path} /',
                'umount-all',
            ])
        finally:
            if os.path.exists(tar_path):
                os.remove(tar_path)


def copy_c_library(staging_dir: str, toolchain_prefix: str, target_triple: str, arch_config: dict):
    """Copy C library and runtime files into a staging directory."""
    ld_name = arch_config['ld_musl_name']

    sysroot = os.path.join(toolchain_prefix, target_triple, 'sysroot', 'usr')
    if not os.path.exists(sysroot):
        print(f"   WARNING: toolchain sysroot not found at {sysroot}")
        return

    print('   CP toolchain C libraries')

    sysroot_lib = os.path.join(sysroot, 'lib')
    target_lib = os.path.join(staging_dir, 'lib')

    if os.path.exists(sysroot_lib):
        os.makedirs(target_lib, exist_ok=True)

        libc_src = os.path.join(sysroot_lib, 'libc.so')
        if os.path.exists(libc_src):
            shutil.copy2(libc_src, target_lib)

        for crt_file in ['Scrt1.o', 'crt1.o', 'crti.o', 'crtn.o']:
            crt_path = os.path.join(sysroot_lib, crt_file)
            if os.path.exists(crt_path):
                shutil.copy2(crt_path, target_lib)

        lib_libc = os.path.join(target_lib, 'libc.so')
        lib_ld = os.path.join(target_lib, ld_name)
        if os.path.exists(lib_libc):
            print(f'   CP /lib/{ld_name}')
            shutil.copy2(lib_libc, lib_ld)


def install_grub(image_path: str, staging_dir: str):
    """Install GRUB (BIOS) into an HD image using staged /boot content."""
    bootdir = os.path.join(staging_dir, 'boot')
    os.makedirs(bootdir, exist_ok=True)

    print('   GRUB-INSTALL')
    _run_command([
        'grub-install',
        '--target=i386-pc',
        f'--boot-directory={bootdir}',
        '--recheck',
        '--force',
        '--no-floppy',
        image_path,
    ])


def _ensure_iso_boot_image(staging_dir: str):
    """Create El Torito boot image for GRUB if grub.cfg exists."""
    grub_dir = Path(staging_dir) / 'boot' / 'grub'
    grub_cfg = grub_dir / 'grub.cfg'
    if not grub_cfg.exists():
        return

    eltorito_dir = grub_dir / 'i386-pc'
    eltorito_dir.mkdir(parents=True, exist_ok=True)
    eltorito_img = eltorito_dir / 'eltorito.img'

    _run_command([
        'grub-mkstandalone',
        '-O', 'i386-pc-eltorito',
        '-o', str(eltorito_img),
        f'boot/grub/grub.cfg={grub_cfg}',
    ])


def create_iso_image(iso_path: str, source_dir: str, volume_label: str = 'VALKYRIE'):
    """Create ISO image from staged content using xorriso."""
    source_dir = os.path.abspath(source_dir)
    if not os.path.isdir(source_dir):
        raise RuntimeError(f'Source directory not found: {source_dir}')

    _ensure_iso_boot_image(source_dir)

    boot_img = Path(source_dir) / 'boot' / 'grub' / 'i386-pc' / 'eltorito.img'

    cmd = [
        'xorriso',
        '-as', 'mkisofs',
        '-R',
        '-J',
        '-V', volume_label,
        '-o', iso_path,
    ]

    if boot_img.exists():
        cmd.extend([
            '-b', 'boot/grub/i386-pc/eltorito.img',
            '-no-emul-boot',
            '-boot-load-size', '4',
            '-boot-info-table',
        ])
    else:
        print('   WARNING: boot/grub/grub.cfg not found; creating non-bootable ISO')

    cmd.append(source_dir)
    _run_command(cmd)
