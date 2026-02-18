# SPDX-License-Identifier: BSD-3-Clause
"""
Valkyrie OS Build System

Main build configuration file using SCons.
"""

import os
from pathlib import Path
import platform

from SCons.Variables import Variables, EnumVariable
from SCons.Environment import Environment

from scripts.scons.arch import get_arch_config, get_supported_archs
from scripts.scons.disk import get_supported_filesystems
from scripts.scons.phony_targets import PhonyTargets
from scripts.scons.utility import ParseSize


# Determine whether the host toolchain matches the requested target.
def host_arch_matches_target(target_triple: str) -> bool:
    host = platform.machine().lower()
    # Normalize common names but preserve specific x86 variants like i686
    host_map = {
        'x86_64': 'x86_64', 'amd64': 'x86_64',
        'i386': 'i386', 'i486': 'i486', 'i586': 'i586', 'i686': 'i686',
        'aarch64': 'aarch64', 'arm64': 'aarch64', 'armv7l': 'arm',
    }
    host_norm = host_map.get(host, host)
    target_arch = target_triple.split('-')[0].lower()

    # Exact match is fine
    if host_norm == target_arch:
        return True

    # Treat x86/i686/i386 family as compatible
    if host_norm.startswith('i') and host_norm.endswith('86') \
       and target_arch.startswith('i') and target_arch.endswith('86'):
        return True

    return False


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
        'imageFS': 'fat32',
        'buildType': 'full',
        'imageSize': '250m',
        'toolchain': '/opt/cross/',
        'outputFile': 'valkyrieos',
        'outputFormat': 'hd',
        'kernelName': 'valkyrix',
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
                 default='hd',
                 allowed_values=('hd', 'iso')),
)

VARS.Add('imageSize',
         help='Disk image size (supports k/m/g suffixes)',
         default='250m',
         converter=ParseSize)

VARS.Add('toolchain',
         help='Path to toolchain directory',
         default='toolchain/')

VARS.Add('outputFile',
         help='Output image filename (without extension)',
         default='valkyrieos')

VARS.Add('kernelName',
         help='Kernel executable name',
         default='valkyrix')


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
    
    # Configuration-specific flags
    if env['config'] == 'debug':
        env.Append(CCFLAGS=['-O0', '-DDEBUG', '-g'])
    else:
        env.Append(CCFLAGS=['-O3', '-DRELEASE', '-s'])
    
    # Architecture define
    arch_config = get_arch_config(env['arch'])
    env.Append(CCFLAGS=[f'-D{arch_config["define"]}'])
    
    return env


# =============================================================================
# Target Environment
# =============================================================================

def create_target_environment(host_env):
    """Create the cross-compilation target environment."""
    arch = host_env['arch']
    arch_config = get_arch_config(arch)
    
    toolchain_dir = Path(host_env['toolchain']).resolve()
    toolchain_bin = toolchain_dir / 'bin'
    toolchain_gcc_libs = (toolchain_dir / 'lib' / 'gcc' / 
                          arch_config['target_triple'] / DEPS['gcc'])
    
    prefix = arch_config['toolchain_prefix']

    use_native = host_arch_matches_target(arch_config['target_triple'])

    # Choose tool names: prefixed cross-tools or native host tools
    if use_native:
        tools = dict(
            AS='as', AR='ar', CC='gcc', CXX='g++', LD='g++', RANLIB='ranlib', STRIP='strip'
        )
    else:
        tools = dict(
            AS=f'{prefix}as', AR=f'{prefix}ar', CC=f'{prefix}gcc', CXX=f'{prefix}g++',
            LD=f'{prefix}g++', RANLIB=f'{prefix}ranlib', STRIP=f'{prefix}strip'
        )

    env = host_env.Clone(
        # Cross-compiler or native tools
        **tools,

        # Toolchain paths
        TOOLCHAIN_PREFIX=str(toolchain_dir),
        TOOLCHAIN_LIBGCC=str(toolchain_gcc_libs),

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
    
    # Add toolchain to PATH
    env['ENV']['PATH'] += os.pathsep + str(toolchain_bin)
    
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
    
    # Phony targets using Python scripts
    arch = TARGET_ENVIRONMENT['arch']
    target = TARGET_ENVIRONMENT['TARGET_TRIPLE']
    toolchain_dir = HOST_ENVIRONMENT['toolchain']
    media_kind = 'cdrom' if TARGET_ENVIRONMENT['outputFormat'] == 'iso' else 'disk'
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, media_kind, image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, media_kind, image[0].path],
        bochs=['python3', './scripts/base/bochs.py', media_kind, image[0].path],
        toolchain=['python3', './scripts/base/toolchain.py', toolchain_dir, '-t', target],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
    
    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)

elif build_type == 'image':
    SConscript('image/SConscript', variant_dir=variant_dir, duplicate=0)
    Import('image')
    
    arch = TARGET_ENVIRONMENT['arch']
    target = TARGET_ENVIRONMENT['TARGET_TRIPLE']
    toolchain_dir = HOST_ENVIRONMENT['toolchain']
    media_kind = 'cdrom' if TARGET_ENVIRONMENT['outputFormat'] == 'iso' else 'disk'
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, media_kind, image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, media_kind, image[0].path],
        bochs=['python3', './scripts/base/bochs.py', media_kind, image[0].path],
        toolchain=['python3', './scripts/base/toolchain.py', toolchain_dir, '-t', target],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
    
    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)

else:
    # Minimal phony targets
    target = TARGET_ENVIRONMENT.get('TARGET_TRIPLE', 'unknown')
    toolchain_dir = HOST_ENVIRONMENT['toolchain']
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        toolchain=['python3', './scripts/base/toolchain.py', toolchain_dir, '-t', target],
        fformat=['python3', './scripts/base/format.py'],
        deps=['python3', './scripts/base/dependencies.py'],
    )
