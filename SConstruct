# SPDX-License-Identifier: BSD-3-Clause

from pathlib import Path
from SCons.Variables import *
from SCons.Environment import *
from SCons.Node import *
from scripts.scons.phony_targets import PhonyTargets
from scripts.scons.utility import ParseSize

VARS = Variables('scripts/scons/config.py', ARGUMENTS)
VARS.AddVariables(
    EnumVariable("config",
                 help="Build configuration",
                 default="debug",
                 allowed_values=("debug", "release")),

    EnumVariable("arch", 
                 help="Target architecture", 
                 default="i686",
                 allowed_values=("i686", "x64")),

    EnumVariable("imageFS",
                 help="Type of image",
                 default="fat32",
                 allowed_values=("fat12", "fat16", "fat32", "ext2")),
    EnumVariable("buildType",
                 help="What to build",
                 default="full",
                 allowed_values=("full", "kernel", "usr")),
    )
VARS.Add("imageSize", 
         help="The size of the image, will be rounded up to the nearest multiple of 512. " +
              "You can use suffixes (k/m/g). " +
              "For floppies, the size is fixed to 1.44MB.",
         default="250m",
         converter=ParseSize)
VARS.Add("toolchain", 
         help="Path to toolchain directory.",
         default="toolchain/")
VARS.Add("outputFile", 
         help="The name of final image.",
         default="image")
VARS.Add("outputFormat", 
         help="The extension of the disk image.",
         default="img")
VARS.Add("kernelName", 
         help="The name of the executable.",
         default="valkyrix")

DEPS = {
    'binutils': '2.37',
    'gcc': '15.2.0'
}


#
# ***  Host environment ***
#

HOST_ENVIRONMENT = Environment(variables=VARS,
    ENV = os.environ,
    CFLAGS = ['-std=c99'],
    CXXFLAGS = ['-std=c++17'],
    CCFLAGS = ['-g'],
    STRIP = 'strip'
)

if HOST_ENVIRONMENT['config'] == 'debug':
    HOST_ENVIRONMENT.Append(CCFLAGS = ['-O0', '-DDEBUG'])
else:
    HOST_ENVIRONMENT.Append(CCFLAGS = ['-O3', '-DRELEASE'])

#
# *** Define architecture macros ***
#

if HOST_ENVIRONMENT['arch'] == 'i686':
    HOST_ENVIRONMENT.Append(CCFLAGS = ['-DI686'])
elif HOST_ENVIRONMENT['arch'] == 'x64':
    HOST_ENVIRONMENT.Append(CCFLAGS = ['-DX64'])
else:
    HOST_ENVIRONMENT.Append(CCFLAGS = ['-DNOARCH'])

#
# ***  Target environment ***
#

platform_prefix = ''
if HOST_ENVIRONMENT['arch'] == 'i686':
    platform_prefix = 'i686-elf-'
    target = 'i686-elf'
elif HOST_ENVIRONMENT['arch'] == 'x64':
    platform_prefix = 'x86_64-elf-'
    target = 'x86_64-elf'
else:
    platform_prefix = ''
    target = 'unknown'

toolchainDir = Path(HOST_ENVIRONMENT['toolchain'], target).resolve()
toolchainBin = Path(toolchainDir, 'bin')
toolchainGccLibs = Path(toolchainDir, 'lib', 'gcc', platform_prefix.removesuffix('-'), DEPS['gcc'])

TARGET_ENVIRONMENT = HOST_ENVIRONMENT.Clone(
    AS = f'{platform_prefix}as',
    AR = f'{platform_prefix}ar',
    CC = f'{platform_prefix}gcc',
    CXX = f'{platform_prefix}g++',
    LD = f'{platform_prefix}g++',
    RANLIB = f'{platform_prefix}ranlib',
    STRIP = f'{platform_prefix}strip',

    # toolchain
    TOOLCHAIN_PREFIX = str(toolchainDir),
    TOOLCHAIN_LIBGCC = str(toolchainGccLibs),
    BINUTILS_URL = f'https://ftp.gnu.org/gnu/binutils/binutils-{DEPS["binutils"]}.tar.xz',
    GCC_URL = f'https://ftp.gnu.org/gnu/gcc/gcc-{DEPS["gcc"]}/gcc-{DEPS["gcc"]}.tar.xz',
)


TARGET_ENVIRONMENT.Append(
    ASFLAGS = [
        '-m32',
        '-g'
    ],
    CCFLAGS = [
        '-ffreestanding',
        '-nostdlib',
        '-fstack-protector-all'
        # Note: -fPIC added per-target (e.g., kernel SConscript) for dynamic linking support
    ],
    CXXFLAGS = [
        '-fno-exceptions',
        '-fno-rtti',
    ],
    LINKFLAGS = [
        '-nostdlib'
        # Note: --unresolved-symbols=ignore-all added in kernel SConscript for dynamic linking
    ],
    LIBS = ['gcc'],
    LIBPATH = [ str(toolchainGccLibs) ]
)

# Ensure custom command strings also apply to target builds
TARGET_ENVIRONMENT.Replace(ASCOMSTR        = "   AS      $SOURCE",
                           ASPPCOMSTR      = "   AS      $SOURCE",
                           CCCOMSTR        = "   CC      $SOURCE",
                           CXXCOMSTR       = "   CXX     $SOURCE",
                           SHCCCOMSTR      = "   CC      $SOURCE",
                           SHCXXCOMSTR     = "   CXX     $SOURCE",
                           LINKCOMSTR      = "   LD      $TARGET",
                           SHLINKCOMSTR    = "   LD      $TARGET",
                           ARCOMSTR        = "   AR      $TARGET",
                           RANLIBCOMSTR    = "   RANLIB  $TARGET")

TARGET_ENVIRONMENT['ENV']['PATH'] += os.pathsep + str(toolchainBin)

Help(VARS.GenerateHelpText(HOST_ENVIRONMENT))
Export('HOST_ENVIRONMENT')
Export('TARGET_ENVIRONMENT')

variantDir = 'build/{0}_{1}'.format(TARGET_ENVIRONMENT['arch'], TARGET_ENVIRONMENT['config'])

buildType = TARGET_ENVIRONMENT['buildType']

if buildType in ('full', 'usr'):
    SConscript('usr/SConscript', variant_dir=variantDir + '/usr', duplicate=0)

if buildType in ('full', 'kernel'):
    SConscript('kernel/SConscript', variant_dir=variantDir + '/kernel', duplicate=0)

if buildType == 'full':
    SConscript('image/SConscript', variant_dir=variantDir, duplicate=0)
    Import('image')

    # Phony targets
    PhonyTargets(HOST_ENVIRONMENT, 
                 run=['./scripts/base/qemu.sh', 'disk', image[0].path],
                 debug=['./scripts/base/gdb.sh', 'disk', image[0].path],
                 bochs=['./scripts/base/bochs.sh', 'disk', image[0].path],
                 toolchain=['./scripts/base/toolchain.sh', HOST_ENVIRONMENT['toolchain'], target],
                 fformat=['./scripts/base/format.sh'])

    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)

if buildType == 'image':
    SConscript('image/SConscript', variant_dir=variantDir, duplicate=0)

    # Phony targets
    PhonyTargets(HOST_ENVIRONMENT, 
                 run=['./scripts/base/qemu.sh', 'disk', image[0].path],
                 debug=['./scripts/base/gdb.sh', 'disk', image[0].path],
                 bochs=['./scripts/base/bochs.sh', 'disk', image[0].path],
                 toolchain=['./scripts/base/toolchain.sh', HOST_ENVIRONMENT['toolchain'], target],
                 fformat=['./scripts/base/format.sh'])

    Depends('run', image)
    Depends('debug', image)
    Depends('bochs', image)
    
else:
    # Phony targets without image-dependent ones
    PhonyTargets(HOST_ENVIRONMENT,
                 toolchain=['./scripts/base/toolchain.sh', HOST_ENVIRONMENT['toolchain'], target],
                 fformat=['./scripts/base/format.sh'])