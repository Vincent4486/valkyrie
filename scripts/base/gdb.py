#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
GDB debugging launcher for Valkyrie OS.

Launches QEMU in debug mode and connects GDB for kernel debugging.
"""

import argparse
import os
import subprocess
import sys
import tempfile

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from scripts.scons.arch import get_arch_config, get_supported_archs


# Default settings
DEFAULT_MEMORY = '32M'


def get_qemu_debug_args(arch: str, image_type: str, image_path: str,
                        memory: str = DEFAULT_MEMORY) -> str:
    """Get QEMU arguments for GDB pipe mode."""
    arch_config = get_arch_config(arch)
    
    args = [
        arch_config['qemu_system'],
        '-S',                    # Freeze CPU at startup
        '-gdb', 'stdio',         # GDB on stdio for pipe connection
        '-m', memory,
    ]
    
    if image_type == 'floppy':
        args.extend(['-fda', image_path])
    elif image_type == 'disk':
        args.extend(['-hda', image_path])
    elif image_type == 'cdrom':
        args.extend(['-cdrom', image_path])
    else:
        raise ValueError(f"Unknown image type: {image_type}")
    
    return ' '.join(args)


def generate_gdb_script(qemu_cmd: str, kernel_symbols: str = None) -> str:
    """Generate GDB initialization script."""
    script = f'''# Auto-generated GDB script for Valkyrie OS debugging
target remote | {qemu_cmd}
set disassembly-flavor intel
'''
    
    if kernel_symbols:
        script += f'symbol-file {kernel_symbols}\n'
    
    script += '''# Set a breakpoint at the bootloader entry point
b *0x7c00
layout asm
'''
    
    return script


def run_gdb(arch: str, image_type: str, image_path: str,
            memory: str = DEFAULT_MEMORY, kernel_symbols: str = None,
            gdb_cmd: str = 'gdb') -> int:
    """Run GDB with QEMU for kernel debugging.
    
    Args:
        arch: Target architecture
        image_type: Type of image ('disk', 'cdrom' or 'floppy')
        image_path: Path to the disk image
        memory: Memory size for QEMU
        kernel_symbols: Path to kernel ELF with debug symbols
        gdb_cmd: GDB executable name
    
    Returns:
        GDB exit code
    """
    qemu_cmd = get_qemu_debug_args(arch, image_type, image_path, memory)
    gdb_script = generate_gdb_script(qemu_cmd, kernel_symbols)
    
    # Write script to temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.gdb', delete=False) as f:
        f.write(gdb_script)
        script_path = f.name
    
    try:
        result = subprocess.call([gdb_cmd, '-x', script_path])
    finally:
        # Clean up script file
        os.unlink(script_path)
    
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Debug Valkyrie OS with GDB',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('image_type', choices=['disk', 'cdrom', 'floppy'],
                        help='Type of disk image')
    parser.add_argument('image', help='Path to disk image file')
    parser.add_argument('-a', '--arch', default='i686',
                        choices=get_supported_archs(),
                        help='Target architecture (default: i686)')
    parser.add_argument('-m', '--memory', default=DEFAULT_MEMORY,
                        help=f'Memory size (default: {DEFAULT_MEMORY})')
    parser.add_argument('-k', '--kernel', 
                        help='Path to kernel ELF with debug symbols')
    parser.add_argument('--gdb', default='gdb',
                        help='GDB executable name (default: gdb)')
    
    args = parser.parse_args()
    
    if not os.path.exists(args.image):
        print(f"Error: Image file not found: {args.image}", file=sys.stderr)
        sys.exit(1)
    
    sys.exit(run_gdb(
        arch=args.arch,
        image_type=args.image_type,
        image_path=args.image,
        memory=args.memory,
        kernel_symbols=args.kernel,
        gdb_cmd=args.gdb,
    ))


if __name__ == '__main__':
    main()
