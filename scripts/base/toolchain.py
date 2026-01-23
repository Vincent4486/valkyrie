#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause

import os
import subprocess
import platform
import shutil
from SCons.Script import Import

# Import the SCons environment
env = Import('HOST_ENVIRONMENT')

# Versions from env
BINUTILS_VERSION = env['DEPS']['binutils']
GCC_VERSION = env['DEPS']['gcc']
MUSL_VERSION = "1.2.5"  # Not in env, keep hardcoded

# URLs from env
BINUTILS_URL = env['BINUTILS_URL']
GCC_URL = env['GCC_URL']
MUSL_URL = f"https://musl.libc.org/releases/musl-{MUSL_VERSION}.tar.gz"

def detect_os():
    return platform.system()

def run_command(cmd, cwd=None, env_vars=None):
    """Run a command with subprocess, raising on error."""
    result = subprocess.run(cmd, shell=True, cwd=cwd, env=env_vars, check=True)
    return result

def download_and_extract(url, filename, extract_cmd):
    """Download and extract archive if not already present."""
    if not os.path.exists(filename.replace('.tar.xz', '').replace('.tar.gz', '')):
        run_command(f"wget {url}")
        run_command(extract_cmd)

def build_binutils(toolchain_prefix, target, os_name):
    binutils_src = f"binutils-{BINUTILS_VERSION}"
    binutils_build = f"binutils-build-{BINUTILS_VERSION}-{target}"

    download_and_extract(BINUTILS_URL, f"binutils-{BINUTILS_VERSION}.tar.xz", f"tar -xf binutils-{BINUTILS_VERSION}.tar.xz")

    os.makedirs(binutils_build, exist_ok=True)
    os.chdir(binutils_build)

    env_vars = os.environ.copy()
    env_vars.update({
        'CFLAGS': '',
        'ASMFLAGS': '',
        'CC': '',
        'CXX': '',
        'LD': '',
        'ASM': '',
        'LINKFLAGS': '',
        'LIBS': ''
    })

    binutils_opts = f"--prefix={toolchain_prefix} --target={target} --with-sysroot={toolchain_prefix} --disable-nls --disable-werror"
    if os_name == "Darwin":
        binutils_opts += " --with-system-zlib --enable-zstd"

    run_command(f"../{binutils_src}/configure {binutils_opts}", env=env_vars)
    run_command("make -j8", env=env_vars)
    run_command("make install", env=env_vars)

    os.chdir("..")

def build_gcc_stage1(toolchain_prefix, target, os_name):
    gcc_src = f"gcc-{GCC_VERSION}"
    gcc_build = f"gcc-build-{GCC_VERSION}-{target}"

    download_and_extract(GCC_URL, f"gcc-{GCC_VERSION}.tar.xz", f"tar -xf gcc-{GCC_VERSION}.tar.xz")

    os.makedirs(gcc_build, exist_ok=True)
    os.chdir(gcc_build)

    env_vars = os.environ.copy()
    env_vars.update({
        'CFLAGS': '',
        'ASMFLAGS': '',
        'LD': '',
        'ASM': '',
        'LINKFLAGS': '',
        'LIBS': ''
    })

    gcc_opts = f"--prefix={toolchain_prefix} --target={target} --disable-nls --enable-languages=c --without-headers"
    if os_name == "Darwin":
        gcc_opts += " --with-system-zlib --with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr --with-mpc=/opt/homebrew/opt/libmpc"

    run_command(f"../{gcc_src}/configure {gcc_opts}", env=env_vars)
    run_command("make -j8 all-gcc all-target-libgcc", env=env_vars)
    run_command("make install-gcc install-target-libgcc", env=env_vars)

    os.chdir("..")

def build_musl(toolchain_prefix, target, os_name):
    musl_src = f"musl-{MUSL_VERSION}"
    musl_build = f"musl-build-{MUSL_VERSION}-{target}"
    musl_target = target.replace('-elf', '-linux-musl')

    download_and_extract(MUSL_URL, f"musl-{MUSL_VERSION}.tar.gz", f"tar -xf musl-{MUSL_VERSION}.tar.gz")

    os.makedirs(musl_build, exist_ok=True)
    os.chdir(musl_build)

    env_vars = os.environ.copy()
    env_vars['PATH'] = f"{toolchain_prefix}/bin:{env_vars.get('PATH', '')}"
    env_vars.update({
        'CC': f"{target}-gcc",
        'CXX': f"{target}-g++",
        'LD': f"{target}-ld",
        'AR': f"{target}-ar",
        'RANLIB': f"{target}-ranlib",
        'STRIP': f"{target}-strip",
        'CFLAGS': '',
        'ASMFLAGS': '',
        'LINKFLAGS': '',
        'LIBS': ''
    })

    run_command(f"../{musl_src}/configure --prefix={toolchain_prefix}/usr --host={musl_target} --enable-static --disable-shared", env=env_vars)
    run_command("make -j8", env=env_vars)
    run_command("make install", env=env_vars)

    os.chdir("..")

def build_gcc_stage2(toolchain_prefix, target, os_name):
    gcc_build_stage2 = f"gcc-build-stage2-{GCC_VERSION}-{target}"

    os.makedirs(gcc_build_stage2, exist_ok=True)
    os.chdir(gcc_build_stage2)

    env_vars = os.environ.copy()
    env_vars['PATH'] = f"{toolchain_prefix}/bin:{env_vars.get('PATH', '')}"
    env_vars.update({
        'CFLAGS': '',
        'ASMFLAGS': '',
        'LD': '',
        'ASM': '',
        'LINKFLAGS': '',
        'LIBS': ''
    })

    gcc_opts = f"--prefix={toolchain_prefix} --target={target} --with-sysroot={toolchain_prefix} --disable-nls --enable-languages=c"
    if os_name == "Darwin":
        gcc_opts += " --with-system-zlib --with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr --with-mpc=/opt/homebrew/opt/libmpc"

    run_command(f"../gcc-{GCC_VERSION}/configure {gcc_opts}", env=env_vars)
    run_command("make -j8", env=env_vars)
    run_command("make install", env=env_vars)

    os.chdir("..")

def clean(toolchains_dir, target):
    """Clean build directories."""
    dirs_to_clean = [
        f"binutils-{BINUTILS_VERSION}",
        f"gcc-{GCC_VERSION}",
        f"musl-{MUSL_VERSION}",
        f"binutils-build-{BINUTILS_VERSION}-{target}",
        f"gcc-build-{GCC_VERSION}-{target}",
        f"musl-build-{MUSL_VERSION}-{target}",
        f"gcc-build-stage2-{GCC_VERSION}-{target}",
        target  # the toolchain prefix dir
    ]
    for d in dirs_to_clean:
        if os.path.exists(d):
            shutil.rmtree(d)

def build_toolchain(toolchains_dir='toolchain', target='i686-elf', clean_build=False):
    """Main build function, can be called from SCons or standalone."""
    os_name = detect_os()

    if clean_build:
        clean(toolchains_dir, target)
        return

    os.makedirs(toolchains_dir, exist_ok=True)
    os.chdir(toolchains_dir)
    toolchain_prefix = os.path.join(os.getcwd(), target)

    # Uncomment the builds as needed
    # build_binutils(toolchain_prefix, target, os_name)
    # build_gcc_stage1(toolchain_prefix, target, os_name)
    build_musl(toolchain_prefix, target, os_name)
    # build_gcc_stage2(toolchain_prefix, target, os_name)

def main():
    parser = argparse.ArgumentParser(description="Build cross-compilation toolchain")
    parser.add_argument('-c', '--clean', action='store_true', help='Clean build directories')
    parser.add_argument('toolchains_dir', nargs='?', default='toolchain', help='Toolchains directory')
    parser.add_argument('target', nargs='?', default='i686-elf', help='Target architecture (default: i686-elf)')

    args = parser.parse_args()

    build_toolchain(args.toolchains_dir, args.target, args.clean)
