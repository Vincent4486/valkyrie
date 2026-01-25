# SPDX-License-Identifier: BSD-3-Clause
"""
Valkyrie OS Build System

Main build configuration file using SCons.
"""

import os
from pathlib import Path

from SCons.Variables import Variables, EnumVariable
from SCons.Environment import Environment

from scripts.scons.arch import get_arch_config, get_supported_archs
from scripts.scons.disk import get_supported_filesystems
from scripts.scons.phony_targets import PhonyTargets
from scripts.scons.utility import ParseSize


# =============================================================================
# Build Variables
# =============================================================================

VARS = Variables('scripts/scons/config.py', ARGUMENTS)

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
                 allowed_values=('full', 'kernel', 'usr')),
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

VARS.Add('outputFormat',
         help='Disk image file extension',
         default='img')

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
    
    env = host_env.Clone(
        # Cross-compiler tools
        AS=f'{prefix}as',
        AR=f'{prefix}ar',
        CC=f'{prefix}gcc',
        CXX=f'{prefix}g++',
        LD=f'{prefix}g++',
        RANLIB=f'{prefix}ranlib',
        STRIP=f'{prefix}strip',
        
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
    
    # Architecture-specific flags
    env.Append(
        ASFLAGS=arch_config['asflags'],
        CCFLAGS=arch_config['ccflags'],
        CXXFLAGS=['-fno-exceptions', '-fno-rtti'],
        LINKFLAGS=arch_config['linkflags'],
        LIBS=['gcc'],
        LIBPATH=[str(toolchain_gcc_libs)],
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
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, 'disk', image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, 'disk', image[0].path],
        bochs=['python3', './scripts/base/bochs.py', 'disk', image[0].path],
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
    
    PhonyTargets(
        HOST_ENVIRONMENT,
        run=['python3', './scripts/base/qemu.py', '-a', arch, 'disk', image[0].path],
        debug=['python3', './scripts/base/gdb.py', '-a', arch, 'disk', image[0].path],
        bochs=['python3', './scripts/base/bochs.py', 'disk', image[0].path],
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
