#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Makefile-compatible disk output script.
Wraps the image/SConscript logic to build the disk/ISO image.
"""

import os
import sys
import argparse
import shutil

# Add the root directory to the search path for SCons scripts
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from scripts.scons.disk import (
    VOLUME_LABEL,
    copy_toolchain_runtime_to_staging,
    create_bootable_iso,
    format_partition_image,
    get_grub_modules,
    get_partition_type_id,
    generate_grub_config,
)
from scripts.scons.arch import get_arch_config
from scripts.scons.utility import ParseSize

def main():
    parser = argparse.ArgumentParser(description="Valkyrie OS Disk Builder (Makefile Wrapper)")
    parser.add_argument("--arch", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--fs", required=True)
    parser.add_argument("--size", required=True)
    parser.add_argument("--format", required=True)
    parser.add_argument("--toolchain", required=True)
    parser.add_argument("--kernel-exe", required=True)
    parser.add_argument("--kernel-name", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--libs", nargs='*', default=[])
    parser.add_argument("--apps", nargs='*', default=[])
    parser.add_argument("--root-dir", required=True)
    
    args = parser.parse_args()

    # Reuse SConscript logic (simplified version of image/SConscript build_disk)
    staging_dir = os.path.dirname(args.output)
    if not staging_dir: staging_dir = "."
    staging_dir = os.path.join(staging_dir, args.format)
    
    shutil.rmtree(staging_dir, ignore_errors=True)
    os.makedirs(staging_dir, exist_ok=True)
    
    print(f"   STAGE -> {staging_dir}")

    # 1. Kernel
    bootdir = os.path.join(staging_dir, 'boot')
    os.makedirs(bootdir, exist_ok=True)
    shutil.copy2(args.kernel_exe, bootdir)

    # 2. GRUB Config
    generate_grub_config(
        os.path.join(bootdir, 'grub'),
        args.format,
        config=args.config,
        kernel_name=args.kernel_name,
        volume_label=VOLUME_LABEL,
    )

    # 3. Toolchain Runtime
    arch_config = get_arch_config(args.arch)
    copy_toolchain_runtime_to_staging(
        staging_dir, args.toolchain, arch_config['target_triple'], arch_config
    )

    # 4. Libraries
    if args.libs:
        libdir = os.path.join(staging_dir, 'usr', 'lib')
        os.makedirs(libdir, exist_ok=True)
        for lib in args.libs:
            shutil.copy2(lib, libdir)

    # 5. Apps
    if args.apps:
        bindir = os.path.join(staging_dir, 'usr', 'bin')
        os.makedirs(bindir, exist_ok=True)
        for app in args.apps:
            shutil.copy2(app, bindir)

    # 6. Extra Files (image/root)
    root_src = args.root_dir
    if os.path.exists(root_src):
        for root, dirs, files in os.walk(root_src):
            for file in files:
                src_path = os.path.join(root, file)
                rel_path = os.path.relpath(src_path, root_src)
                dst_path = os.path.join(staging_dir, rel_path)
                os.makedirs(os.path.dirname(dst_path), exist_ok=True)
                shutil.copy2(src_path, dst_path)

    # 7. Final Image Assembly
    # (Assuming build_disk logic from image/SConscript is moved to scripts/scons/disk.py 
    # or implemented here as in the SConscript)
    
    # ISO build is simple:
    if args.format == 'iso':
        create_bootable_iso(staging_dir, args.output, volume_label=VOLUME_LABEL)
        return

    # RAW Disk build (requires external tools)
    # Re-implementing simplified version from SConscript:
    import subprocess
    
    # ParseSize for MB
    size_bytes = ParseSize(args.size)
    total_size_mb = max(1, (size_bytes + (1024 * 1024 - 1)) // (1024 * 1024))
    image_dir = os.path.dirname(args.output) or "."
    tmp_part = os.path.join(image_dir, 'part.tmp')
    grub_core = os.path.join(image_dir, 'grub.core')
    boot_head = os.path.join(image_dir, 'boot_head.img')
    grub_path = '/usr/lib/grub/i386-pc'
    
    part_type_id = get_partition_type_id(args.fs)
    part_start_mb = 1
    part_size_mb = total_size_mb - part_start_mb
    part_start_sector = part_start_mb * 2048

    try:
        print("   GRUB-MKIMAGE")
        subprocess.run([
            'grub-mkimage', '-O', 'i386-pc', '-o', grub_core,
            '-p', '(hd0,msdos1)/boot/grub'
        ] + get_grub_modules(args.fs), check=True)

        with open(os.path.join(grub_path, 'boot.img'), 'rb') as boot_img, \
             open(grub_core, 'rb') as core_img, \
             open(boot_head, 'wb') as out_img:
            out_img.write(boot_img.read())
            out_img.write(core_img.read())

        subprocess.run(['truncate', '-s', f'{part_size_mb}M', tmp_part], check=True)
        format_partition_image(tmp_part, args.fs, volume_label=VOLUME_LABEL)

        subprocess.run(['truncate', '-s', f'{total_size_mb}M', args.output], check=True)
        
        # MBR Setup via guestfish
        guestfish_mbr = f'part-init /dev/sda mbr\npart-add /dev/sda p {part_start_sector} -1\npart-set-mbr-id /dev/sda 1 {part_type_id}\nquit\n'
        p = subprocess.Popen(['guestfish', '-a', args.output], stdin=subprocess.PIPE)
        p.communicate(input=guestfish_mbr.encode())

        # Splicing and bootloader writing:
        subprocess.run(['dd', f'if={tmp_part}', f'of={args.output}', 'bs=512', f'seek={part_start_sector}', 'conv=notrunc', 'status=none'], check=True)
        subprocess.run(['dd', f'if={boot_head}', f'of={args.output}', 'bs=446', 'count=1', 'conv=notrunc', 'status=none'], check=True)
        subprocess.run(['dd', f'if={boot_head}', f'of={args.output}', 'bs=512', 'skip=1', 'seek=1', 'conv=notrunc', 'status=none'], check=True)

        # MBR Patch
        with open(args.output, 'r+b') as disk_file:
            disk_file.seek(92)
            disk_file.write(b'\x01\x00\x00\x00')

        # Ingest files
        guestfish_copy = f'mount /dev/sda1 /\ncopy-in {staging_dir}/. /\nquit\n'
        p = subprocess.Popen(['guestfish', '-a', args.output], stdin=subprocess.PIPE)
        p.communicate(input=guestfish_copy.encode())

    finally:
        for tmp_file in (tmp_part, boot_head, grub_core):
            if os.path.exists(tmp_file): os.remove(tmp_file)

if __name__ == "__main__":
    main()
