#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Bochs emulator launcher for Valkyrie OS.

Generates bochs configuration and runs disk, cdrom, or floppy media.
"""

import argparse
import os
import subprocess
import sys
import tempfile


# Default Bochs settings
DEFAULT_MEMORY_MB = 128
DEFAULT_BOCHS_BIOS = '/usr/share/bochs/BIOS-bochs-latest'
DEFAULT_BOCHS_VGA = '/usr/share/bochs/VGABIOS-lgpl-latest'


def generate_bochs_config(image_type: str, image_path: str,
                          memory_mb: int = DEFAULT_MEMORY_MB,
                          bios_path: str = DEFAULT_BOCHS_BIOS,
                          vga_path: str = DEFAULT_BOCHS_VGA,
                          display: str = 'x') -> str:
    """Generate Bochs configuration file content.
    
    Args:
        image_type: Type of image ('disk', 'cdrom' or 'floppy')
        image_path: Path to the disk image
        memory_mb: Memory size in megabytes
        bios_path: Path to Bochs BIOS ROM
        vga_path: Path to Bochs VGA BIOS ROM
        display: Display library ('x', 'sdl2', 'nogui')
    
    Returns:
        Bochs configuration file content
    """
    if image_type == 'floppy':
        disk_cfg = f'floppya: 1_44="{image_path}", status=inserted'
        boot_cfg = 'boot: floppy'
    elif image_type == 'disk':
        disk_cfg = f'ata0-master: type=disk, path="{image_path}", cylinders=1024, heads=4, spt=32'
        boot_cfg = 'boot: disk'
    elif image_type == 'cdrom':
        disk_cfg = f'ata0-master: type=cdrom, path="{image_path}", status=inserted'
        boot_cfg = 'boot: cdrom'
    else:
        raise ValueError(f"Unknown image type: {image_type}")
    
    config = f'''# Auto-generated Bochs configuration for Valkyrie OS
megs: {memory_mb}
romimage: file={bios_path}
vgaromimage: file={vga_path}
mouse: enabled=0
display_library: {display}

{disk_cfg}
{boot_cfg}
'''
    
    return config


def find_bochs_roms() -> tuple:
    """Find Bochs BIOS and VGA ROM files."""
    # Common locations for Bochs ROMs
    search_paths = [
        '/usr/share/bochs',
        '/usr/local/share/bochs',
        '/opt/bochs/share',
    ]
    
    bios_names = ['BIOS-bochs-latest', 'BIOS-bochs-legacy']
    vga_names = ['VGABIOS-lgpl-latest', 'VGABIOS-lgpl-latest.bin']
    
    bios_path = None
    vga_path = None
    
    for base in search_paths:
        if not os.path.isdir(base):
            continue
        
        for name in bios_names:
            path = os.path.join(base, name)
            if os.path.exists(path) and bios_path is None:
                bios_path = path
        
        for name in vga_names:
            path = os.path.join(base, name)
            if os.path.exists(path) and vga_path is None:
                vga_path = path
    
    return bios_path or DEFAULT_BOCHS_BIOS, vga_path or DEFAULT_BOCHS_VGA


def run_bochs(image_type: str, image_path: str,
              memory_mb: int = DEFAULT_MEMORY_MB,
              display: str = 'x',
              bochs_cmd: str = 'bochs') -> int:
    """Run Bochs emulator with the specified configuration.
    
    Args:
        image_type: Type of image ('disk', 'cdrom' or 'floppy')
        image_path: Path to the disk image
        memory_mb: Memory size in megabytes
        display: Display library to use
        bochs_cmd: Bochs executable name
    
    Returns:
        Bochs exit code
    """
    bios_path, vga_path = find_bochs_roms()
    
    config = generate_bochs_config(
        image_type=image_type,
        image_path=image_path,
        memory_mb=memory_mb,
        bios_path=bios_path,
        vga_path=vga_path,
        display=display,
    )
    
    # Write config to temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.bochsrc', delete=False) as f:
        f.write(config)
        config_path = f.name
    
    try:
        result = subprocess.call([bochs_cmd, '-q', '-f', config_path])
    finally:
        os.unlink(config_path)
    
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Run Valkyrie OS in Bochs',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('image_type', choices=['disk', 'cdrom', 'floppy'],
                        help='Type of disk image')
    parser.add_argument('image', help='Path to disk image file')
    parser.add_argument('-m', '--memory', type=int, default=DEFAULT_MEMORY_MB,
                        help=f'Memory size in MB (default: {DEFAULT_MEMORY_MB})')
    parser.add_argument('-d', '--display', default='x',
                        choices=['x', 'sdl2', 'nogui'],
                        help='Display library (default: x)')
    parser.add_argument('--bochs', default='bochs',
                        help='Bochs executable name')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.image):
        print(f"Error: Image file not found: {args.image}", file=sys.stderr)
        sys.exit(1)
    
    sys.exit(run_bochs(
        image_type=args.image_type,
        image_path=args.image,
        memory_mb=args.memory,
        display=args.display,
        bochs_cmd=args.bochs,
    ))


if __name__ == '__main__':
    main()
