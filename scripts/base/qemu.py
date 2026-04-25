#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause

import argparse
import os
import subprocess
import sys

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from scripts.scons.arch import GetArchConfig, GetSupportedArchitectures

DEFAULT_MEMORY = '4G'
DEFAULT_SMP = 1

def get_qemu_base_args(arch: str, memory: str = DEFAULT_MEMORY, 
                       smp: int = DEFAULT_SMP, debug_console: bool = True) -> list:
    ArchConfig = GetArchConfig(arch)
    
    args = [
        ArchConfig['QemuSystem'],
        '-m', memory,
        '-machine', ArchConfig['QemuMachine'],
        '-smp', str(smp),
        '-device', 'VGA,vgamem_mb=64'
    ]
    
    if debug_console:
        args.extend(['-debugcon', 'stdio'])
    
    return args


def add_disk_args(args: list, image_type: str, image_path: str) -> list:
    """Add disk/floppy/cdrom arguments to QEMU command."""
    if image_type == 'floppy':
        args.extend(['-fda', image_path])
    elif image_type == 'disk':
        args.extend([
            '-drive', f'file={image_path},format=raw,if=ide,index=0,media=disk'
        ])
    elif image_type == 'cdrom':
        args.extend(['-cdrom', image_path])
    else:
        raise ValueError(f"Unknown image type: {image_type}")
    
    return args


def run_qemu(arch: str, image_type: str, image_path: str, 
             memory: str = DEFAULT_MEMORY, smp: int = DEFAULT_SMP,
             extra_args: list = None, debug_console: bool = True) -> int:
    """Run QEMU with the specified configuration.
    
    Args:
        arch: Target architecture (i686, x86_64, aarch64)
        image_type: Type of image ('disk', 'cdrom' or 'floppy')
        image_path: Path to the disk image
        memory: Memory size (e.g., '4G', '512M')
        smp: Number of CPU cores
        extra_args: Additional QEMU arguments
        debug_console: Enable debug console on stdio
    
    Returns:
        QEMU exit code
    """
    args = get_qemu_base_args(arch, memory, smp, debug_console)
    args = add_disk_args(args, image_type, image_path)
    
    if extra_args:
        args.extend(extra_args)
    
    print(f"Running: {' '.join(args)}")
    return subprocess.call(args)


# =============================================================================
# Main Entry Point
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Run Valecium OS in QEMU',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('image_type', choices=['disk', 'cdrom', 'floppy'],
                        help='Type of disk image')
    parser.add_argument('image', help='Path to disk image file')
    parser.add_argument('-a', '--arch', default='i686',
                        choices=GetSupportedArchitectures(),
                        help='Target architecture (default: i686)')
    parser.add_argument('-m', '--memory', default=DEFAULT_MEMORY,
                        help=f'Memory size (default: {DEFAULT_MEMORY})')
    parser.add_argument('-s', '--smp', type=int, default=DEFAULT_SMP,
                        help=f'Number of CPU cores (default: {DEFAULT_SMP})')
    parser.add_argument('--no-debug-console', action='store_true',
                        help='Disable debug console output')
    parser.add_argument('extra', nargs='*',
                        help='Additional QEMU arguments')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.image):
        print(f"Error: Image file not found: {args.image}", file=sys.stderr)
        sys.exit(1)
    
    sys.exit(run_qemu(
        arch=args.arch,
        image_type=args.image_type,
        image_path=args.image,
        memory=args.memory,
        smp=args.smp,
        extra_args=args.extra,
        debug_console=not args.no_debug_console,
    ))


if __name__ == '__main__':
    main()
