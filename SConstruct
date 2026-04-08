# SPDX-License-Identifier: BSD-3-Clause
"""
Valecium OS Build System

Main build configuration file using SCons.
"""

import os
from pathlib import Path
import shutil
import subprocess

from SCons.Variables import Variables, EnumVariable
from SCons.Environment import Environment

from scripts.scons.arch import get_arch_config, get_supported_archs
from scripts.scons.disk import get_supported_filesystems
from scripts.scons.phony_targets import PhonyTargets
from scripts.scons.utility import ParseSize


def get_git_short_hash() -> str:
    """Return the current git short commit hash or an empty string."""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--short=7', 'HEAD'],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ''


def query_compiler_value(compiler: str, flag: str) -> str:
    """Query a compiler for a single value and return a stripped string."""
    try:
        result = subprocess.run(
            [compiler, flag],
            check=True,
            capture_output=True,
            text=True,
        )
        return result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ''


def infer_gcc_lib_dir(compiler: str, compiler_path: str, default_target: str) -> str:
    """Infer GCC runtime library directory from compiler binary location."""
    if not compiler_path or compiler_path == '<not found>':
        return ''

    compiler_bin = Path(compiler_path).resolve()
    gcc_base = compiler_bin.parent / '..' / 'lib' / 'gcc'

    machine = query_compiler_value(compiler, '-dumpmachine') or default_target
    version = query_compiler_value(compiler, '-dumpversion')

    if machine and version:
        candidate = (gcc_base / machine / version).resolve()
        if candidate.exists():
            return str(candidate)

    if machine:
        machine_dir = (gcc_base / machine).resolve()
        if machine_dir.exists() and machine_dir.is_dir():
            versions = sorted([p for p in machine_dir.iterdir() if p.is_dir()])
            if versions:
                return str(versions[-1])

    libgcc_a = query_compiler_value(compiler, '-print-file-name=libgcc.a')
    if libgcc_a and libgcc_a != 'libgcc.a':
        libgcc_path = Path(libgcc_a)
        if libgcc_path.exists():
            return str(libgcc_path.parent)

    return ''


def resolve_build_tools(arch: str):
    """Resolve build tools from PATH using preferred cross prefixes.

    Preference order:
    1) {arch}-linux-musl-*
    2) {arch}-elf-*
    3) unprefixed host tools
    """
    prefix_candidates = [f'{arch}-linux-musl-', f'{arch}-elf-', '']

    selected_prefix = ''
    for prefix in prefix_candidates:
        gcc_name = f'{prefix}gcc' if prefix else 'gcc'
        if shutil.which(gcc_name):
            selected_prefix = prefix
            break

    tool_bases = {
        'AS': 'as',
        'AR': 'ar',
        'CC': 'gcc',
        'CXX': 'g++',
        'LD': 'g++',
        'RANLIB': 'ranlib',
        'STRIP': 'strip',
    }

    tools = {}
    tool_paths = {}

    for key, base in tool_bases.items():
        preferred_name = f'{selected_prefix}{base}' if selected_prefix else base
        preferred_path = shutil.which(preferred_name)
        if preferred_path:
            tools[key] = preferred_name
            tool_paths[key] = preferred_path
            continue

        fallback_path = shutil.which(base)
        if fallback_path:
            tools[key] = base
            tool_paths[key] = fallback_path
        else:
            # Keep the preferred command name so SCons errors are explicit.
            tools[key] = preferred_name
            tool_paths[key] = '<not found>'

    return tools, tool_paths, selected_prefix


# =============================================================================
# Build Variables
# =============================================================================

# Autogenerate a simple Python-style config file at project root named
# `.config` (if it doesn't exist) and then load it with SCons Variables.
config_path = Path('.config')
if not config_path.exists():
    default_config = {
        'config': 'debug',
        'arch': 'i686',
        'kernelVersion': '0.28',
        'imageFS': 'fat32',
        'buildType': 'full',
        'imageSize': '250m',
        'outputFile': 'valeciumos',
        'outputFormat': 'img',
        'kernelName': 'valeciumx',
    }
    with open(config_path, 'w') as cf:
        for k, v in default_config.items():
            cf.write(f"{k} = {repr(v)}\n")

VARS = Variables(str(config_path), ARGUMENTS)

VARS.AddVariables(
    EnumVariable('config',
                 help='Build configuration',
                 default='debug',
                 allowed_values=('debug', 'release')),
    
    EnumVariable('arch',
                 help='Target architecture',
                 default='i686',
                 allowed_values=tuple(get_supported_archs())),
    
    EnumVariable('imageFS',
                 help='Filesystem type for disk image',
                 default='fat32',
                 allowed_values=tuple(get_supported_filesystems())),
    
    EnumVariable('buildType',
                 help='What to build',
                 default='full',
                 allowed_values=('full', 'kernel', 'usr', 'image')),

    EnumVariable('outputFormat',
                 help='Output image format',
                 default='img',
                 allowed_values=('img', 'iso')),
)

VARS.Add('imageSize',
         help='Disk image size (supports k/m/g suffixes)',
         default='250m',
         converter=ParseSize)

VARS.Add('outputFile',
         help='Output image filename (without extension)',
         default='valeciumos')

VARS.Add('kernelName',
         help='Kernel executable name',
         default='valeciumx')

VARS.Add('kernelVersion',
         help='Kernel version string in MAJOR.MINOR form',
         default='0.28')

# =============================================================================
# Dependency Versions
# =============================================================================

DEPS = {
    'binutils': '2.45',
    'gcc': '15.2.0',
}


# =============================================================================
# Host Environment
# =============================================================================

def create_host_environment():
    """Create the host build environment."""
    env = Environment(
        variables=VARS,
        ENV=os.environ,
        CFLAGS=['-std=c99'],
        CXXFLAGS=['-std=c++17'],
        STRIP='strip',
    )

    # Version mode:
    # - debug: git short hash
    # - release: configured version from .config
    configured_version = str(env['kernelVersion'])
    if env['config'] == 'debug':
        git_hash = get_git_short_hash()
        env['kernelVersion'] = git_hash if git_hash else configured_version
    else:
        env['kernelVersion'] = configured_version
    env['kernelOutputName'] = f'{env["kernelName"]}-{env["kernelVersion"]}'
    
    # Configuration-specific flags
    if env['config'] == 'debug':
        env.Append(CCFLAGS=['-O0', '-DDEBUG', '-g'])
    else:
        env.Append(CCFLAGS=['-O3', '-DRELEASE', '-s'])
    
    # Architecture define
    arch_config = get_arch_config(env['arch'])
    env.Append(CCFLAGS=[
        f'-D{arch_config["define"]}',
        f'-DKERNEL_VERSION=\\"{env["kernelVersion"]}\\"',
    ])
    
    return env


# =============================================================================
# Target Environment
# =============================================================================

def create_target_environment(host_env):
    """Create the cross-compilation target environment.

    The required prefixed toolchain binaries must already be available in PATH.
    """
    arch = host_env['arch']
    arch_config = get_arch_config(arch)

    tools, tool_paths, selected_prefix = resolve_build_tools(arch)

    cc = tools['CC']
    cc_path = tool_paths.get('CC', '')
    gcc_lib_dir = infer_gcc_lib_dir(cc, cc_path, arch_config['target_triple'])
    target_sysroot = query_compiler_value(cc, '-print-sysroot')
    if gcc_lib_dir:
        crtbegin_obj = str(Path(gcc_lib_dir) / 'crtbegin.o')
        crtend_obj = str(Path(gcc_lib_dir) / 'crtend.o')
    else:
        crtbegin_obj = query_compiler_value(cc, '-print-file-name=crtbegin.o')
        crtend_obj = query_compiler_value(cc, '-print-file-name=crtend.o')

    selected_desc = selected_prefix if selected_prefix else 'unprefixed host tools'
    print(f"Using build tool prefix for {arch}: {selected_desc}")
    print('Resolved build tools:')
    for key in ('CC', 'CXX', 'AR', 'AS', 'LD', 'RANLIB', 'STRIP'):
        print(f"  {key:<6} {tools[key]:<24} -> {tool_paths[key]}")
    print(f"  {'GCCLIB':<6} {'(inferred)':<24} -> {gcc_lib_dir if gcc_lib_dir else '<not found>'}")

    env = host_env.Clone(
        # Cross-compiler or native tools
        **tools,

        # Runtime paths resolved from the selected compiler
        TARGET_SYSROOT=target_sysroot,
        GCC_LIB_DIR=gcc_lib_dir,
        CRTBEGIN_OBJ=crtbegin_obj,
        CRTEND_OBJ=crtend_obj,

        # Architecture info
        ARCH_CONFIG=arch_config,
        TARGET_TRIPLE=arch_config['target_triple'],

        # Download URLs
        BINUTILS_URL=f'https://ftp.gnu.org/gnu/binutils/binutils-{DEPS["binutils"]}.tar.xz',
        GCC_URL=f'https://ftp.gnu.org/gnu/gcc/gcc-{DEPS["gcc"]}/gcc-{DEPS["gcc"]}.tar.xz',
    )
    
    # Keep only generic C++ flags here; architecture-specific and
    # target-specific flags are applied in each SConscript so kernel
    # and userland can have distinct settings.
    env.Append(
        CXXFLAGS=['-fno-exceptions', '-fno-rtti'],
    )
    
    # Custom build output strings
    env.Replace(
        ASCOMSTR='   AS      $SOURCE',
        ASPPCOMSTR='   AS      $SOURCE',
        CCCOMSTR='   CC      $SOURCE',
        CXXCOMSTR='   CXX     $SOURCE',
        SHCCCOMSTR='   CC      $SOURCE',
        SHCXXCOMSTR='   CXX     $SOURCE',
        LINKCOMSTR='   LD      $TARGET',
        SHLINKCOMSTR='   LD      $TARGET',
        ARCOMSTR='   AR      $TARGET',
        RANLIBCOMSTR='   RANLIB  $TARGET',
    )
    
    return env


# =============================================================================
# Build Setup
# =============================================================================

HOST_ENVIRONMENT = create_host_environment()
TARGET_ENVIRONMENT = create_target_environment(HOST_ENVIRONMENT)

# Generate help text
Help(VARS.GenerateHelpText(HOST_ENVIRONMENT))

# Export environments
Export('HOST_ENVIRONMENT')
Export('TARGET_ENVIRONMENT')

# Build directory
variant_dir = f'build/{TARGET_ENVIRONMENT["arch"]}_{TARGET_ENVIRONMENT["config"]}'
build_type = TARGET_ENVIRONMENT['buildType']


# =============================================================================
# Sub-builds
# =============================================================================

if build_type in ('full', 'usr'):
    SConscript('usr/SConscript', variant_dir=f'{variant_dir}/usr', duplicate=0)

if build_type in ('full', 'kernel'):
    SConscript('kernel/SConscript', variant_dir=f'{variant_dir}/kernel', duplicate=0)

if build_type == 'full':
    SConscript('image/SConscript', variant_dir=variant_dir, duplicate=0)
    Import('image')
    Import('core')
    
    # Phony targets using Python scripts
    arch = TARGET_ENVIRONMENT['arch']
    target = TARGET_ENVIRONMENT['TARGET_TRIPLE']
    media_kind = 'cdrom' if TARGET_ENVIRONMENT['outputFormat'] == 'iso' else 'disk'
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, media_kind, image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, media_kind, image[0].path, core[0].path],
        bochs=['python3', './scripts/base/bochs.py', media_kind, image[0].path],
        toolchain=['python3', './scripts/base/toolchain.py', 'toolchain/', '-t', target, '--ensure'],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
    
    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)

elif build_type == 'image':
    SConscript('image/SConscript', variant_dir=variant_dir, duplicate=0)
    Import('image')
    Import('core')
    
    arch = TARGET_ENVIRONMENT['arch']
    target = TARGET_ENVIRONMENT['TARGET_TRIPLE']
    media_kind = 'cdrom' if TARGET_ENVIRONMENT['outputFormat'] == 'iso' else 'disk'
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, media_kind, image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, media_kind, image[0].path, core[0].path],
        bochs=['python3', './scripts/base/bochs.py', media_kind, image[0].path, '-d', 'x'],
        toolchain=['python3', './scripts/base/toolchain.py', 'toolchain/', '-t', target, '--ensure'],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
    
    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)

else:
    # Minimal phony targets
    target = TARGET_ENVIRONMENT.get('TARGET_TRIPLE', 'unknown')
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        toolchain=['python3', './scripts/base/toolchain.py', 'toolchain/', '-t', target, '--ensure'],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
