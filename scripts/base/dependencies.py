#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Dependency installer for Valecium OS development.

Detects the Linux distribution and installs required packages.
"""

import argparse
import multiprocessing
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

DEPENDENCIES = {
    'debian': {
        'packages': [
            # === Core Build Tools ===
            'build-essential',
            'gcc',
            'g++',
            'make',
            
            # === Development Libraries (Required for Toolchain Build) ===
            'libmpfr-dev',
            'libgmp-dev',
            'libmpc-dev',
            
            # === Python & Scripting ===
            'python3',
            'python3-pip',
            'python3-sh',
            'scons',
            'bash',
            'grep',
            'coreutils',
            
            # === Filesystem & Disk Tools ===
            'dosfstools',
            'e2fsprogs',
            'xorriso',
            
            # === Boot & Image Creation ===
            'grub-pc-bin',
            'guestfish',
            
            # === Emulation & Debugging ===
            'qemu-system-x86',
            'qemu-system-aarch64',
            'gdb',
            
            # === Code Quality ===
            'clang-format',
            
            # === Documentation ===
            'pandoc',
            'asciidoctor',
            'texinfo',
        ],
        'update_cmd': ['apt-get', 'update'],
        'install_cmd': ['apt-get', 'install', '-y'],
    },
    'fedora': {
        'packages': [
            # === Core Build Tools ===
            'gcc',
            'gcc-c++',
            'make',
            
            # === Development Libraries (Required for Toolchain Build) ===
            'mpfr-devel',
            'gmp-devel',
            'libmpc-devel',
            
            # === Python & Scripting ===
            'python3',
            'python3-pip',
            'scons',
            'bash',
            'grep',
            'coreutils',
            
            # === Filesystem & Disk Tools ===
            'dosfstools',
            'e2fsprogs',
            'xorriso',
            
            # === Boot & Image Creation ===
            'grub2-tools',
            'grub2-tools-extra',
            'guestfs-tools',
            
            # === Emulation & Debugging ===
            'qemu-system-x86',
            'qemu-system-aarch64',
            'gdb',
            
            # === Code Quality ===
            'clang-tools-extra',
            
            # === Documentation ===
            'pandoc',
            'asciidoctor',
            'texinfo',
        ],
        'update_cmd': None,
        'install_cmd': ['dnf', 'install', '-y'],
    },
    'arch': {
        'packages': [
            # === Core Build Tools ===
            'base-devel',
            'gcc',
            'make',
            
            # === Development Libraries (Required for Toolchain Build) ===
            'mpfr',
            'gmp',
            'libmpc',
            
            # === Python & Scripting ===
            'python',
            'python-pip',
            'scons',
            'bash',
            'grep',
            'coreutils',
            
            # === Filesystem & Disk Tools ===
            'dosfstools',
            'e2fsprogs',
            'xorriso',
            
            # === Boot & Image Creation ===
            'grub',
            'libguestfs',
            
            # === Emulation & Debugging ===
            'qemu-system-x86',
            'qemu-system-aarch64',
            'gdb',
            
            # === Code Quality ===
            'clang',
            
            # === Documentation ===
            'pandoc',
            'asciidoctor',
            'texinfo',
        ],
        'update_cmd': ['pacman', '-Syu', '--noconfirm'],
        'install_cmd': ['pacman', '-S', '--noconfirm'],
    },
    'suse': {
        'packages': [
            # === Core Build Tools ===
            'gcc',
            'gcc-c++',
            'make',
            
            # === Development Libraries (Required for Toolchain Build) ===
            'mpfr-devel',
            'gmp-devel',
            'libmpc-devel',
            
            # === Python & Scripting ===
            'python3',
            'python3-pip',
            'scons',
            'bash',
            'grep',
            'coreutils',
            
            # === Filesystem & Disk Tools ===
            'dosfstools',
            'e2fsprogs',
            'xorriso',
            
            # === Boot & Image Creation ===
            'grub2',
            'grub2-i386-pc',
            'guestfs-tools',
            
            # === Emulation & Debugging ===
            'qemu-x86',
            'qemu-system-aarch64',
            'gdb',
            
            # === Code Quality ===
            'clang',
            
            # === Documentation ===
            'pandoc',
            'asciidoctor',
            'texinfo',
        ],
        'update_cmd': None,
        'install_cmd': ['zypper', 'install', '-y'],
    },
    'alpine': {
        'packages': [
            # === Core Build Tools ===
            'build-base',
            'gcc',
            'make',
            
            # === Development Libraries (Required for Toolchain Build) ===
            'mpfr-dev',
            'mpc1-dev',
            'gmp-dev',
            
            # === Python & Scripting ===
            'python3',
            'py3-pip',
            'scons',
            'bash',
            'grep',
            'coreutils',
            
            # === Filesystem & Disk Tools ===
            'dosfstools',
            'e2fsprogs',
            'xorriso',
            
            # === Boot & Image Creation ===
            'grub',
            'libguestfs-tools',
            
            # === Emulation & Debugging ===
            'qemu-system-x86_64',
            'qemu-system-aarch64',
            'gdb',
            
            # === Code Quality ===
            'clang',
            
            # === Documentation ===
            'pandoc',
            'asciidoctor',
            'texinfo',
        ],
        'update_cmd': ['apk', 'update'],
        'install_cmd': ['apk', 'add'],
    },
}


RUNTIME_DEPENDENCIES = {
    'musl': {
        'version': '1.2.6',
        'url': 'https://musl.libc.org/releases/musl-{version}.tar.gz',
    },
}


def get_cpu_count() -> int:
    """Get number of CPUs for parallel runtime builds."""
    return multiprocessing.cpu_count()


def run_command(cmd: list, cwd: str = None, env: dict = None) -> subprocess.CompletedProcess:
    """Run a command with logging."""
    print(f"$ {' '.join(cmd)}")
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(cmd, cwd=cwd, env=merged_env, check=True)


def download_file(url: str, dest: Path):
    """Download a file if it does not already exist."""
    if dest.exists():
        print(f"Already downloaded: {dest.name}")
        return

    print(f"Downloading: {url}")
    urllib.request.urlretrieve(url, str(dest))


def extract_archive(archive: Path, dest_dir: Path):
    """Extract a source archive if needed."""
    print(f"Extracting: {archive.name}")
    with tarfile.open(archive) as tar:
        tar.extractall(str(dest_dir))


def build_RuntimeDependencies(output_dir: str, target: str, jobs: int = None) -> int:
    """Build runtime dependencies (musl) into a build-local path.

    Args:
        output_dir: Build-local output root (for example build/i686_debug/dependencies/musl)
        target: Target triple (for example i686-linux-musl)
        jobs: Number of parallel make jobs

    Returns:
        0 on success, non-zero on failure.
    """
    jobs = jobs or get_cpu_count()
    output_root = Path(output_dir).resolve()

    install_usr = output_root / 'usr'
    libc_path = install_usr / 'lib' / 'libc.so'
    crt1_path = install_usr / 'lib' / 'crt1.o'

    if libc_path.exists() and crt1_path.exists():
        print(f"Runtime dependencies already installed in: {output_root}")
        return 0

    required_tools = [
        f'{target}-gcc',
        f'{target}-as',
        f'{target}-ld',
        f'{target}-ar',
        f'{target}-ranlib',
        f'{target}-strip',
    ]
    missing_tools = [tool for tool in required_tools if not shutil.which(tool)]
    if missing_tools:
        print('Error: required cross-tools not found in PATH:', file=sys.stderr)
        for tool in missing_tools:
            print(f'  - {tool}', file=sys.stderr)
        return 1

    musl_cfg = RUNTIME_DEPENDENCIES['musl']
    version = musl_cfg['version']
    url = musl_cfg['url'].format(version=version)
    archive_name = url.split('/')[-1]

    work_root = output_root / '.work'
    src_root = work_root / 'src'
    src_root.mkdir(parents=True, exist_ok=True)
    output_root.mkdir(parents=True, exist_ok=True)

    archive_path = src_root / archive_name
    src_path = src_root / f'musl-{version}'
    build_path = work_root / f'musl-{target}'

    download_file(url, archive_path)
    if not src_path.exists():
        extract_archive(archive_path, src_root)

    if build_path.exists():
        shutil.rmtree(build_path)
    build_path.mkdir(parents=True, exist_ok=True)

    cross_env = {
        'CC': f'{target}-gcc',
        'AS': f'{target}-as',
        'LD': f'{target}-ld',
        'AR': f'{target}-ar',
        'RANLIB': f'{target}-ranlib',
        'STRIP': f'{target}-strip',
    }

    run_command(
        [
            str(src_path / 'configure'),
            '--prefix=/usr',
            f'--host={target}',
            '--enable-static',
            '--enable-shared',
        ],
        cwd=str(build_path),
        env=cross_env,
    )

    run_command(['make', f'-j{jobs}'], cwd=str(build_path), env=cross_env)
    run_command(
        ['make', 'install', f'DESTDIR={output_root}'],
        cwd=str(build_path),
        env=cross_env,
    )

    if work_root.exists():
        shutil.rmtree(work_root)

    print(f"Runtime dependencies installed to: {output_root}")
    return 0


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
        description='Install development dependencies for Valecium OS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('-d', '--distro',
                        choices=list(DEPENDENCIES.keys()),
                        help='Force specific distribution (auto-detected by default)')
    parser.add_argument('--build-runtime', action='store_true',
                        help='Build runtime dependencies (musl) into a build-local output path')
    parser.add_argument('-t', '--target',
                        help='Target triple for runtime dependency build')
    parser.add_argument('--output',
                        help='Output directory for runtime dependency artifacts')
    parser.add_argument('-j', '--jobs', type=int, default=get_cpu_count(),
                        help=f'Parallel jobs for runtime build (default: {get_cpu_count()})')
    parser.add_argument('-n', '--dry-run', action='store_true',
                        help='Print commands without executing')
    parser.add_argument('--no-sudo', action='store_true',
                        help='Run commands without sudo')
    parser.add_argument('-l', '--list', action='store_true',
                        help='List packages for detected/specified distribution')
    parser.add_argument('-y', '--yes', action='store_true',
                        help='Skip confirmation prompt (for non-interactive use)')
    
    args = parser.parse_args()

    if args.build_runtime:
        if not args.target:
            print('Error: --target is required with --build-runtime', file=sys.stderr)
            sys.exit(1)
        if not args.output:
            print('Error: --output is required with --build-runtime', file=sys.stderr)
            sys.exit(1)

        try:
            sys.exit(build_RuntimeDependencies(
                output_dir=args.output,
                target=args.target,
                jobs=args.jobs,
            ))
        except subprocess.CalledProcessError as e:
            print(f"Error: command failed with exit code {e.returncode}", file=sys.stderr)
            sys.exit(1)
    
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
    if not args.dry_run and not args.yes:
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
