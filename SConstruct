# SPDX-License-Identifier: BSD-3-Clause
"""
Valecium OS Build System

Main build configuration file using SCons.
"""

import os
import shutil
import subprocess
from pathlib import Path

from SCons.Environment import Environment
from SCons.Variables import EnumVariable, Variables

from scripts.scons.arch import GetArchConfig, GetSupportedArchitectures
from scripts.scons.disk import GetSupportedFilesystems
from scripts.scons.utility import ParseSize


def GetGitHash() -> str:
    try:
        Result = subprocess.run(
            ['git', 'rev-parse', '--short=7', 'HEAD'],
            check=True,
            capture_output=True,
            text=True,
        )
        return Result.stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return ''


def ResolveTools(Arch: str):
    Prefixes = [f'{Arch}-linux-musl-', f'{Arch}-elf-', '']

    Selected = ''
    for Prefix in Prefixes:
        Gcc = f'{Prefix}gcc' if Prefix else 'gcc'
        if shutil.which(Gcc):
            Selected = Prefix
            break

    Bases = {
        'AS': 'as',
        'AR': 'ar',
        'CC': 'gcc',
        'LD': 'gcc',
        'RANLIB': 'ranlib',
        'STRIP': 'strip',
    }

    Tools = {}
    Paths = {}

    for Key, Base in Bases.items():
        Preferred = f'{Selected}{Base}' if Selected else Base
        PreferredPath = shutil.which(Preferred)
        if PreferredPath:
            Tools[Key] = Preferred
            Paths[Key] = PreferredPath
            continue

        FallbackPath = shutil.which(Base)
        if FallbackPath:
            Tools[Key] = Base
            Paths[Key] = FallbackPath
        else:
            Tools[Key] = Preferred
            Paths[Key] = '<not found>'

    return Tools, Paths, Selected


ConfigPath = Path('.config')
if not ConfigPath.exists():
    DefaultConfig = {
        'BuildConfig': 'debug',
        'BuildArch': 'i686',
        'ProjVersion': '0.28',
        'ImageFs': 'fat32',
        'BuildType': 'full',
        'ImageSize': '250m',
        'ImageName': 'valeciumos',
        'ImageFormat': 'img',
        'KernelName': 'valeciumx',
        'BootType': 'bios'
    }
    with open(ConfigPath, 'w', encoding='utf-8') as CfgFile:
        for Key, Value in DefaultConfig.items():
            CfgFile.write(f"{Key} = {repr(Value)}\n")

Vars = Variables(str(ConfigPath), ARGUMENTS)

Vars.AddVariables(
    EnumVariable('BuildConfig',
                 help='Build configuration',
                 default='debug',
                 allowed_values=('debug', 'release')),
    
    EnumVariable('BuildArch',
                 help='Target architecture',
                 default='i686',
                 allowed_values=tuple(GetSupportedArchitectures())),
    
    EnumVariable('ImageFs',
                 help='Filesystem type for disk image',
                 default='fat32',
                 allowed_values=tuple(GetSupportedFilesystems())),
    
    EnumVariable('BuildType',
                 help='What to build',
                 default='full',
                 allowed_values=('full', 'kernel', 'usr', 'image', 'bootloader')),

    EnumVariable('ImageFormat',
                 help='Output image format',
                 default='img',
                 allowed_values=('img', 'iso')),
    EnumVariable('BootType',
                 help='Boot type',
                 default='bios',
                 allowed_values=('bios', 'efi')),
)

Vars.Add('ImageSize',
         help='Disk image size (supports k/m/g suffixes)',
         default='250m',
         converter=ParseSize)

Vars.Add('ImageName',
         help='Output image filename (without extension)',
         default='valeciumos')

Vars.Add('KernelName',
         help='Kernel executable name',
         default='valeciumx')

Vars.Add('ProjVersion',
         help='Kernel version string in MAJOR.MINOR form',
         default='0.28')

Deps = {
    'binutils': '2.45',
    'gcc': '15.2.0',
}


def CreateHostEnvironment():
    Env = Environment(
        variables=Vars,
        ENV=os.environ,
        CFLAGS=['-std=c99'],
        STRIP='strip',
    )

    Version = str(Env['ProjVersion'])
    if Env['BuildConfig'] == 'debug':
        Git = GetGitHash()
        Env['ProjVersion'] = Git if Git else Version
    else:
        Env['ProjVersion'] = Version
    Env['KernelOutputName'] = f'{Env["KernelName"]}-{Env["ProjVersion"]}'
    
    if Env['BuildConfig'] == 'debug':
        Env.Append(CCFLAGS=['-O0', '-DDEBUG', '-g'])
    else:
        Env.Append(CCFLAGS=['-O3', '-DRELEASE', '-s'])
    
    ArchitectureConfig = GetArchConfig(Env['BuildArch'])
    KernelVersionMacro = 'KERNEL' + '_VERSION'
    Env.Append(CCFLAGS=[
        f'-D{ArchitectureConfig["Define"]}',
        f'-D{KernelVersionMacro}=\\"{Env["ProjVersion"]}\\"',
    ])
    
    return Env


def CreateTargetEnvironment(HostEnv):
    Arch = HostEnv['BuildArch']
    ArchitectureConfig = GetArchConfig(Arch)

    Tools, ToolPaths, Prefix = ResolveTools(Arch)

    Desc = Prefix if Prefix else 'unprefixed host tools'
    print(f"Using build tool prefix for {Arch}: {Desc}")
    print('Resolved build tools:')
    for Key in ('CC', 'AR', 'AS', 'LD', 'RANLIB', 'STRIP'):
        print(f"  {Key:<6} {Tools[Key]:<24} -> {ToolPaths[Key]}")

    Env = HostEnv.Clone(
        **Tools,
        ArchitectureConfig=ArchitectureConfig,
        TargetTriple=ArchitectureConfig['TargetTriple'],
        BinutilsUrl=f'https://ftp.gnu.org/gnu/binutils/binutils-{Deps["binutils"]}.tar.xz',
        GccUrl=f'https://ftp.gnu.org/gnu/gcc/gcc-{Deps["gcc"]}/gcc-{Deps["gcc"]}.tar.xz',
    )

    Env.Replace(
        ASCOMSTR    ='   AS      $SOURCE',
        ASPPCOMSTR  ='   AS      $SOURCE',
        CCCOMSTR    ='   CC      $SOURCE',
        SHCCCOMSTR  ='   CC      $SOURCE',
        LINKCOMSTR  ='   LD      $TARGET',
        SHLINKCOMSTR='   LD      $TARGET',
        ARCOMSTR    ='   AR      $TARGET',
        RANLIBCOMSTR='   RANLIB  $TARGET',
    )
    

    return Env


HostEnvironment = CreateHostEnvironment()
TargetEnvironment = CreateTargetEnvironment(HostEnvironment)

Help(Vars.GenerateHelpText(HostEnvironment))

Export('HostEnvironment')
Export('TargetEnvironment')

VariantDir = f'build/{TargetEnvironment["BuildArch"]}_{TargetEnvironment["BuildConfig"]}'
BuildType = TargetEnvironment['BuildType']

StageDir = os.path.abspath(os.path.join(VariantDir, 'img'))

TargetEnvironment['ImageStagingDirectory'] = StageDir

if BuildType in ('full', 'usr', 'image'):
    SConscript('usr/SConscript', variant_dir=f'{VariantDir}/usr', duplicate=0)

if BuildType in ('full', 'kernel', 'image'):
    SConscript('kernel/SConscript', variant_dir=f'{VariantDir}/kernel', duplicate=0)

if BuildType in ('full', 'bootloader', 'image'):
    SConscript('bootloader/Sconscript', variant_dir=f'{VariantDir}/bootloader', duplicate=0)

if BuildType in ('full', 'image'):
    SConscript('image/SConscript', variant_dir=VariantDir, duplicate=0)
