#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Dependency installer for Valkyrie OS development.

Detects the Linux distribution and installs required packages.
"""

import argparse
import os
import shutil
import subprocess
import sys


# Package dependencies by distribution family
DEPENDENCIES = {
    'debian': {
        'packages': [
            'build-essential',
            'libmpfr-dev',
            'libgmp-dev',
            'libmpc-dev',
            'gcc',
            'g++',
            'make',
            'python3',
            'python3-pip',
            'scons',
            'python3-sh',
            'dosfstools',
            'parted',
            'grub-pc-bin',
            'grub-common',
            'xorriso',
            'mtools',
            'clang-format',
            'qemu-system-x86',
        ],
        'update_cmd': ['apt-get', 'update'],
        'install_cmd': ['apt-get', 'install', '-y'],
    },
    'fedora': {
        'packages': [
            'mpfr-devel',
            'gmp-devel',
            'libmpc-devel',
            'gcc',
            'gcc-c++',
            'make',
            'python3',
            'python3-pip',
            'scons',
            'python3-sh',
            'dosfstools',
            'parted',
            'grub2-tools',
            'grub2-pc-modules',
            'xorriso',
            'mtools',
            'clang-tools-extra',
            'qemu-system-x86',
        ],
        'update_cmd': None,
        'install_cmd': ['dnf', 'install', '-y'],
    },
    'arch': {
        'packages': [
            'base-devel',
            'mpfr',
            'gmp',
            'libmpc',
            'gcc',
            'make',
            'python',
            'python-pip',
            'scons',
            'python-sh',
            'dosfstools',
            'parted',
            'grub',
            'libisoburn',
            'mtools',
            'clang',
            'qemu-system-x86',
        ],
        'update_cmd': ['pacman', '-Syu', '--noconfirm'],
        'install_cmd': ['pacman', '-S', '--noconfirm'],
    },
    'suse': {
        'packages': [
            'mpfr-devel',
            'gmp-devel',
            'libmpc-devel',
            'gcc',
            'gcc-c++',
            'make',
            'python3',
            'python3-pip',
            'scons',
            'python3-sh',
            'dosfstools',
            'parted',
            'grub2',
            'xorriso',
            'mtools',
            'clang',
            'qemu-x86',
        ],
        'update_cmd': None,
        'install_cmd': ['zypper', 'install', '-y'],
    },
    'alpine': {
        'packages': [
            'build-base',
            'mpfr-dev',
            'mpc1-dev',
            'gmp-dev',
            'gcc',
            'g++',
            'make',
            'python3',
            'py3-pip',
            'scons',
            'py3-sh',
            'dosfstools',
            'parted',
            'grub',
            'xorriso',
            'mtools',
            'clang',
            'qemu-system-x86_64',
        ],
        'update_cmd': ['apk', 'update'],
        'install_cmd': ['apk', 'add'],
    },
}


def detect_distro() -> str:
    """Detect the Linux distribution family."""
    # Check for package managers
    if shutil.which('apt-get'):
        return 'debian'
    elif shutil.which('dnf'):
        return 'fedora'
    elif shutil.which('yum'):
        return 'fedora'
    elif shutil.which('pacman'):
        return 'arch'
    elif shutil.which('zypper'):
        return 'suse'
    elif shutil.which('apk'):
        return 'alpine'
    
    # Fallback: check /etc/os-release
    if os.path.exists('/etc/os-release'):
        with open('/etc/os-release') as f:
            content = f.read().lower()
            if 'debian' in content or 'ubuntu' in content:
                return 'debian'
            elif 'fedora' in content or 'rhel' in content or 'centos' in content:
                return 'fedora'
            elif 'arch' in content:
                return 'arch'
            elif 'suse' in content:
                return 'suse'
            elif 'alpine' in content:
                return 'alpine'
    
    return None


def install_dependencies(distro: str, dry_run: bool = False, 
                         use_sudo: bool = True) -> int:
    """Install dependencies for the specified distribution.
    
    Args:
        distro: Distribution family name
        dry_run: If True, only print commands without executing
        use_sudo: If True, prefix commands with sudo
    
    Returns:
        0 on success, non-zero on error
    """
    if distro not in DEPENDENCIES:
        print(f"Error: Unknown distribution: {distro}", file=sys.stderr)
        print(f"Supported: {', '.join(DEPENDENCIES.keys())}", file=sys.stderr)
        return 1
    
    config = DEPENDENCIES[distro]
    packages = config['packages']
    
    def run_cmd(cmd):
        if use_sudo and os.geteuid() != 0:
            cmd = ['sudo'] + cmd
        
        print(f"$ {' '.join(cmd)}")
        if not dry_run:
            return subprocess.call(cmd)
        return 0
    
    # Update package lists if needed
    if config['update_cmd']:
        result = run_cmd(config['update_cmd'])
        if result != 0 and not dry_run:
            return result
    
    # Install packages
    install_cmd = config['install_cmd'] + packages
    result = run_cmd(install_cmd)
    
    return result


def main():
    parser = argparse.ArgumentParser(
        description='Install development dependencies for Valkyrie OS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('-d', '--distro',
                        choices=list(DEPENDENCIES.keys()),
                        help='Force specific distribution (auto-detected by default)')
    parser.add_argument('-n', '--dry-run', action='store_true',
                        help='Print commands without executing')
    parser.add_argument('--no-sudo', action='store_true',
                        help='Run commands without sudo')
    parser.add_argument('-l', '--list', action='store_true',
                        help='List packages for detected/specified distribution')
    
    args = parser.parse_args()
    
    # Detect or use specified distro
    distro = args.distro or detect_distro()
    if not distro:
        print("Error: Could not detect Linux distribution.", file=sys.stderr)
        print("Please specify with --distro", file=sys.stderr)
        sys.exit(1)
    
    print(f"Distribution: {distro}")
    
    if args.list:
        print(f"\nPackages for {distro}:")
        for pkg in DEPENDENCIES[distro]['packages']:
            print(f"  - {pkg}")
        sys.exit(0)
    
    print()
    if not args.dry_run:
        response = input("Install dependencies? [y/N] ").strip().lower()
        if response not in ('y', 'yes'):
            print("Cancelled.")
            sys.exit(0)
    
    sys.exit(install_dependencies(
        distro=distro,
        dry_run=args.dry_run,
        use_sudo=not args.no_sudo,
    ))


if __name__ == '__main__':
    main()
