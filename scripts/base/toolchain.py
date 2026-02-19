#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Cross-compiler toolchain builder for Valkyrie OS.

Builds binutils, GCC, and musl libc for the specified target architecture.
This replaces the shell-based toolchain.sh with a more flexible Python version.
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

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from scripts.scons.arch import get_arch_config, get_supported_archs


# =============================================================================
# Version Configuration
# =============================================================================

VERSIONS = {
    'binutils': '2.45',
    'gcc': '15.2.0',
    'musl': '1.2.5',
}

URLS = {
    'binutils': 'https://ftp.gnu.org/gnu/binutils/binutils-{version}.tar.xz',
    'gcc': 'https://ftp.gnu.org/gnu/gcc/gcc-{version}/gcc-{version}.tar.xz',
    'musl': 'https://musl.libc.org/releases/musl-{version}.tar.gz',
}


# =============================================================================
# Helper Functions
# =============================================================================

def get_cpu_count() -> int:
    """Get number of CPUs for parallel builds."""
    return multiprocessing.cpu_count()


def detect_os() -> str:
    """Detect host operating system."""
    import platform
    return platform.system()


def run_command(cmd: list, env: dict = None, cwd: str = None, 
                check: bool = True) -> subprocess.CompletedProcess:
    """Run a command with logging."""
    print(f"  $ {' '.join(cmd)}")
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    
    return subprocess.run(cmd, env=merged_env, cwd=cwd, check=check)


def download_file(url: str, dest: str):
    """Download a file with progress indicator."""
    print(f"Downloading: {url}")
    
    def progress_hook(block_num, block_size, total_size):
        downloaded = block_num * block_size
        if total_size > 0:
            percent = min(100, downloaded * 100 // total_size)
            bar = '=' * (percent // 2) + ' ' * (50 - percent // 2)
            print(f"\r  [{bar}] {percent}%", end='', flush=True)
    
    urllib.request.urlretrieve(url, dest, progress_hook)
    print()  # Newline after progress


def extract_archive(archive: str, dest_dir: str):
    """Extract a tar archive."""
    print(f"Extracting: {archive}")
    with tarfile.open(archive) as tar:
        tar.extractall(dest_dir)





# =============================================================================
# Build Classes
# =============================================================================

class ToolchainBuilder:
    """Builds a complete cross-compilation toolchain."""
    
    def __init__(self, prefix: str, target: str, jobs: int = None):
        """
        Args:
            prefix: Toolchain installation prefix (e.g., /opt/toolchain)
            target: Target triple (e.g., i686-linux-musl)
            jobs: Number of parallel jobs (-j flag)
        """
        self.prefix = Path(prefix).resolve()
        self.target = target
        self.jobs = jobs or get_cpu_count()
        
        # Derived paths
        self.sysroot = self.prefix / target / 'sysroot'
        self.bin_dir = self.prefix / 'bin'
        
        # Source/build directories
        self.src_dir = self.prefix / 'src'
        self.build_dir = self.prefix / 'build'
        
        # Environment for builds
        self.build_env = {
            'PATH': f"{self.bin_dir}:{os.environ.get('PATH', '')}",
        }
    
    def setup_directories(self):
        """Create necessary directories."""
        self.prefix.mkdir(parents=True, exist_ok=True)
        self.src_dir.mkdir(exist_ok=True)
        self.build_dir.mkdir(exist_ok=True)
        self.sysroot.mkdir(parents=True, exist_ok=True)
    
    def download_sources(self):
        """Download all source tarballs."""
        for pkg, version in VERSIONS.items():
            url = URLS[pkg].format(version=version)
            filename = url.split('/')[-1]
            dest = self.src_dir / filename
            
            if dest.exists():
                print(f"Already downloaded: {filename}")
                continue
            
            download_file(url, str(dest))
    
    def extract_sources(self):
        """Extract all source tarballs."""
        for pkg, version in VERSIONS.items():
            url = URLS[pkg].format(version=version)
            filename = url.split('/')[-1]
            archive = self.src_dir / filename
            
            # Determine extracted directory name
            if pkg == 'musl':
                src_name = f"musl-{version}"
            else:
                src_name = f"{pkg}-{version}"
            
            src_path = self.src_dir / src_name
            if src_path.exists():
                print(f"Already extracted: {src_name}")
                continue
            
            extract_archive(str(archive), str(self.src_dir))
    
    def _get_configure_opts(self, pkg: str) -> list:
        """Get platform-specific configure options."""
        return []
    
    def build_binutils(self):
        """Build and install binutils."""
        print("\n" + "=" * 60)
        print("Building binutils")
        print("=" * 60)
        
        version = VERSIONS['binutils']
        src_path = self.src_dir / f"binutils-{version}"
        build_path = self.build_dir / f"binutils-{self.target}"
        
        if (self.bin_dir / f"{self.target}-as").exists():
            print("binutils already installed, skipping...")
            return
        
        build_path.mkdir(exist_ok=True)
        
        # Clean environment for configure
        clean_env = {
            'CFLAGS': '',
            'ASMFLAGS': '',
            'CC': '',
            'CXX': '',
            'LD': '',
            'ASM': '',
            'LINKFLAGS': '',
            'LIBS': '',
        }
        
        configure_opts = [
            f"--prefix={self.prefix}",
            f"--target={self.target}",
            f"--with-sysroot={self.sysroot}",
            '--disable-nls',
            '--disable-werror',
        ] + self._get_configure_opts('binutils')
        
        run_command(
            [str(src_path / 'configure')] + configure_opts,
            env={**self.build_env, **clean_env},
            cwd=str(build_path),
        )
        
        run_command(['make', f'-j{self.jobs}'], cwd=str(build_path))
        run_command(['make', 'install'], cwd=str(build_path))
    
    def build_gcc_stage1(self):
        """Build GCC stage 1 (C only, no libc)."""
        print("\n" + "=" * 60)
        print("Building GCC Stage 1")
        print("=" * 60)
        
        version = VERSIONS['gcc']
        src_path = self.src_dir / f"gcc-{version}"
        build_path = self.build_dir / f"gcc-stage1-{self.target}"
        
        if (self.bin_dir / f"{self.target}-gcc").exists():
            print("GCC stage 1 already installed, skipping...")
            return
        
        build_path.mkdir(exist_ok=True)
        
        configure_opts = [
            f"--prefix={self.prefix}",
            f"--target={self.target}",
            f"--with-sysroot={self.sysroot}",
            '--disable-nls',
            '--enable-languages=c',
            '--without-headers',
            '--disable-threads',
            '--disable-isl',
            '--disable-shared',
            '--with-newlib',
        ] + self._get_configure_opts('gcc')
        
        run_command(
            [str(src_path / 'configure')] + configure_opts,
            env=self.build_env,
            cwd=str(build_path),
        )
        
        run_command(
            ['make', f'-j{self.jobs}', 'all-gcc', 'all-target-libgcc'],
            cwd=str(build_path),
        )
        run_command(
            ['make', 'install-gcc', 'install-target-libgcc'],
            cwd=str(build_path),
        )
    
    def build_musl(self):
        """Build and install musl libc."""
        print("\n" + "=" * 60)
        print("Building musl libc")
        print("=" * 60)
        
        version = VERSIONS['musl']
        src_path = self.src_dir / f"musl-{version}"
        build_path = self.build_dir / f"musl-{self.target}"
        
        musl_dest = self.sysroot / 'usr'
        if (musl_dest / 'lib' / 'libc.so').exists():
            print("musl already installed, skipping...")
            return
        
        build_path.mkdir(exist_ok=True)
        
        # Set up cross-compilation environment
        cross_env = {
            'PATH': f"{self.bin_dir}:{os.environ.get('PATH', '')}",
            'CC': f"{self.target}-gcc --sysroot={self.sysroot}",
            'CXX': f"{self.target}-g++ --sysroot={self.sysroot}",
            'AS': f"{self.target}-as",
            'LD': f"{self.target}-ld",
            'AR': f"{self.target}-ar",
            'RANLIB': f"{self.target}-ranlib",
            'STRIP': f"{self.target}-strip",
        }
        
        run_command(
            [
                str(src_path / 'configure'),
                f"--prefix={musl_dest}",
                f"--host={self.target}",
                '--enable-static',
                '--enable-shared',
            ],
            env=cross_env,
            cwd=str(build_path),
        )
        
        run_command(['make', f'-j{self.jobs}'], env=cross_env, cwd=str(build_path))
        run_command(['make', 'install'], env=cross_env, cwd=str(build_path))
    
    def build_gcc_stage2(self):
        """Build GCC stage 2 (full C/C++ with libc support)."""
        print("\n" + "=" * 60)
        print("Building GCC Stage 2")
        print("=" * 60)
        
        version = VERSIONS['gcc']
        src_path = self.src_dir / f"gcc-{version}"
        build_path = self.build_dir / f"gcc-stage2-{self.target}"
        
        # Check if already complete
        if (self.bin_dir / f"{self.target}-g++").exists():
            # Check if C++ support works
            result = subprocess.run(
                [str(self.bin_dir / f"{self.target}-g++"), '--version'],
                capture_output=True,
            )
            if result.returncode == 0:
                print("GCC stage 2 already installed, skipping...")
                return
        
        build_path.mkdir(exist_ok=True)
        
        # Get build triple
        result = subprocess.run(
            [str(src_path / 'config.guess')],
            capture_output=True,
            text=True,
        )
        build_triple = result.stdout.strip()
        
        # Cross-compilation environment for target libraries
        target_env = {
            'PATH': f"{self.bin_dir}:{os.environ.get('PATH', '')}",
            'CC_FOR_TARGET': f"{self.target}-gcc --sysroot={self.sysroot}",
            'CXX_FOR_TARGET': f"{self.target}-g++ --sysroot={self.sysroot}",
            'AR_FOR_TARGET': f"{self.target}-ar",
            'RANLIB_FOR_TARGET': f"{self.target}-ranlib",
            'STRIP_FOR_TARGET': f"{self.target}-strip",
            'CFLAGS_FOR_TARGET': '-O2',
            'CXXFLAGS_FOR_TARGET': '-O2',
        }
        
        # Unset host compiler vars to use system compiler
        for var in ['CC', 'CXX', 'CPPFLAGS', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS']:
            target_env[var] = ''
        
        configure_opts = [
            f"--build={build_triple}",
            f"--host={build_triple}",
            f"--prefix={self.prefix}",
            f"--target={self.target}",
            f"--with-sysroot={self.sysroot}",
            '--disable-nls',
            '--enable-languages=c',
            '--disable-isl',
            '--disable-libsanitizer',
        ] + self._get_configure_opts('gcc')
        
        run_command(
            [str(src_path / 'configure')] + configure_opts,
            env=target_env,
            cwd=str(build_path),
        )
        
        run_command(['make', f'-j{self.jobs}'], env=target_env, cwd=str(build_path))
        run_command(['make', 'install'], env=target_env, cwd=str(build_path))
    
    def build_all(self):
        """Build the complete toolchain."""
        print(f"Building toolchain for {self.target}")
        print(f"  Prefix: {self.prefix}")
        print(f"  Sysroot: {self.sysroot}")
        print(f"  Jobs: {self.jobs}")
        print()
        
        self.setup_directories()
        self.download_sources()
        self.extract_sources()
        
        self.build_binutils()
        self.build_gcc_stage1()
        self.build_musl()
        self.build_gcc_stage2()
        
        print("\n" + "=" * 60)
        print("Toolchain build complete!")
        print("=" * 60)
        print(f"\nAdd to PATH: export PATH=\"{self.bin_dir}:$PATH\"")
    
    def clean(self):
        """Remove build directories (keep sources)."""
        print("Cleaning build directories...")
        if self.build_dir.exists():
            shutil.rmtree(self.build_dir)
            print(f"Removed: {self.build_dir}")
    
    def clean_all(self):
        """Remove all build artifacts and sources."""
        print("Cleaning everything...")
        for path in [self.build_dir, self.src_dir]:
            if path.exists():
                shutil.rmtree(path)
                print(f"Removed: {path}")


# =============================================================================
# Main Entry Point
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Build cross-compilation toolchain for Valkyrie OS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  %(prog)s toolchain/                    # Build for default target (i686-linux-musl)
  %(prog)s toolchain/ -a x64             # Build for x86_64
  %(prog)s toolchain/ -t x86_64-elf      # Build for custom target
  %(prog)s toolchain/ --clean            # Remove build files
  %(prog)s toolchain/ --clean-all        # Remove everything
''',
    )
    
    parser.add_argument('prefix',
                        help='Toolchain installation prefix')
    parser.add_argument('-a', '--arch', choices=get_supported_archs(),
                        help='Target architecture (uses predefined target triple)')
    parser.add_argument('-t', '--target',
                        help='Custom target triple (overrides --arch)')
    parser.add_argument('-j', '--jobs', type=int, default=get_cpu_count(),
                        help=f'Parallel jobs (default: {get_cpu_count()})')
    parser.add_argument('--clean', action='store_true',
                        help='Remove build directories')
    parser.add_argument('--clean-all', action='store_true',
                        help='Remove all build artifacts and sources')
    parser.add_argument('--binutils-only', action='store_true',
                        help='Build only binutils')
    parser.add_argument('--gcc-stage1-only', action='store_true',
                        help='Build only GCC stage 1')
    parser.add_argument('--musl-only', action='store_true',
                        help='Build only musl')
    parser.add_argument('--gcc-stage2-only', action='store_true',
                        help='Build only GCC stage 2')
    
    args = parser.parse_args()
    
    # Determine target triple
    if args.target:
        target = args.target
    elif args.arch:
        target = get_arch_config(args.arch)['target_triple']
    else:
        target = get_arch_config('i686')['target_triple']
    
    builder = ToolchainBuilder(
        prefix=args.prefix,
        target=target,
        jobs=args.jobs,
    )
    
    try:
        if args.clean_all:
            builder.clean_all()
        elif args.clean:
            builder.clean()
        elif args.binutils_only:
            builder.setup_directories()
            builder.download_sources()
            builder.extract_sources()
            builder.build_binutils()
        elif args.gcc_stage1_only:
            builder.setup_directories()
            builder.download_sources()
            builder.extract_sources()
            builder.build_gcc_stage1()
        elif args.musl_only:
            builder.setup_directories()
            builder.download_sources()
            builder.extract_sources()
            builder.build_musl()
        elif args.gcc_stage2_only:
            builder.setup_directories()
            builder.download_sources()
            builder.extract_sources()
            builder.build_gcc_stage2()
        else:
            builder.build_all()
    except subprocess.CalledProcessError as e:
        print(f"\nError: Command failed with exit code {e.returncode}", 
              file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nBuild interrupted.")
        sys.exit(130)


if __name__ == '__main__':
    main()
