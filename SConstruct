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


def BuildRuntimeDepsAction(*_Args, **Kw):
    Target = Kw['target']
    Env = Kw['env']
    OutDir = Path(str(Target[0].abspath)).parent
    OutDir.mkdir(parents=True, exist_ok=True)

    Triple = Env['TargetTriple']
    Jobs = os.cpu_count() or 1

    subprocess.run(
        [
            'python3',
            './scripts/base/dependencies.py',
            '--build-runtime',
            '--target',
            Triple,
            '--output',
            str(OutDir),
            '--jobs',
            str(Jobs),
        ],
        check=True,
    )

    Path(str(Target[0].abspath)).touch()


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
        'CXX': 'g++',
        'LD': 'g++',
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
        'build.config': 'debug',
        'build.arch': 'i686',
        'proj.version': '0.28',
        'image.fs': 'fat32',
        'build.type': 'full',
        'image.size': '250m',
        'image.name': 'valeciumos',
        'image.format': 'img',
        'kernel.name': 'valeciumx',
        'boot.type': 'bios'
    }
    with open(ConfigPath, 'w', encoding='utf-8') as CfgFile:
        for Key, Value in DefaultConfig.items():
            CfgFile.write(f"{Key} = {repr(Value)}\n")

Vars = Variables(str(ConfigPath), ARGUMENTS)

Vars.AddVariables(
    EnumVariable('build.config',
                 help='Build configuration',
                 default='debug',
                 allowed_values=('debug', 'release')),
    
    EnumVariable('build.arch',
                 help='Target architecture',
                 default='i686',
                 allowed_values=tuple(GetSupportedArchitectures())),
    
    EnumVariable('image.fs',
                 help='Filesystem type for disk image',
                 default='fat32',
                 allowed_values=tuple(GetSupportedFilesystems())),
    
    EnumVariable('build.type',
                 help='What to build',
                 default='full',
                 allowed_values=('full', 'kernel', 'usr', 'image', 'bootloader')),

    EnumVariable('image.format',
                 help='Output image format',
                 default='img',
                 allowed_values=('img', 'iso')),
    EnumVariable('boot.type',
                 help='Boot type',
                 default='bios',
                 allowed_values=('bios', 'efi')),
)

Vars.Add('image.size',
         help='Disk image size (supports k/m/g suffixes)',
         default='250m',
         converter=ParseSize)

Vars.Add('image.name',
         help='Output image filename (without extension)',
         default='valeciumos')

Vars.Add('kernel.name',
         help='Kernel executable name',
         default='valeciumx')

Vars.Add('proj.version',
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
        CXXFLAGS=['-std=c++17'],
        STRIP='strip',
    )

    Env['build.config'] = Env['BuildConfig']
    Env['build.arch'] = Env['BuildArch']
    Env['build.type'] = Env['BuildType']
    Env['boot.type'] = Env['BootType']

    Version = str(Env['proj.version'])
    if Env['build.config'] == 'debug':
        Git = GetGitHash()
        Env['proj.version'] = Git if Git else Version
    else:
        Env['proj.version'] = Version
    Env['kernelOutputName'] = f'{Env["kernel.name"]}-{Env["proj.version"]}'
    
    if Env['build.config'] == 'debug':
        Env.Append(CCFLAGS=['-O0', '-DDEBUG', '-g'])
    else:
        Env.Append(CCFLAGS=['-O3', '-DRELEASE', '-s'])
    
    ArchitectureConfig = GetArchConfig(Env['build.arch'])
    KernelVersionMacro = 'KERNEL' + '_VERSION'
    Env.Append(CCFLAGS=[
        f'-D{ArchitectureConfig["Define"]}',
        f'-D{KernelVersionMacro}=\\"{Env["proj.version"]}\\"',
    ])
    
    return Env


def CreateTargetEnvironment(HostEnv):
    Arch = HostEnv['build.arch']
    ArchitectureConfig = GetArchConfig(Arch)

    Tools, ToolPaths, Prefix = ResolveTools(Arch)

    Desc = Prefix if Prefix else 'unprefixed host tools'
    print(f"Using build tool prefix for {Arch}: {Desc}")
    print('Resolved build tools:')
    for Key in ('CC', 'CXX', 'AR', 'AS', 'LD', 'RANLIB', 'STRIP'):
        print(f"  {Key:<6} {Tools[Key]:<24} -> {ToolPaths[Key]}")

    Env = HostEnv.Clone(
        **Tools,
        ArchitectureConfig=ArchitectureConfig,
        TargetTriple=ArchitectureConfig['TargetTriple'],
        BinutilsUrl=f'https://ftp.gnu.org/gnu/binutils/binutils-{Deps["binutils"]}.tar.xz',
        GccUrl=f'https://ftp.gnu.org/gnu/gcc/gcc-{Deps["gcc"]}/gcc-{Deps["gcc"]}.tar.xz',
    )
    
    Env.Append(
        CXXFLAGS=['-fno-exceptions', '-fno-rtti'],
    )
    
    Env.Replace(
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
    
    return Env


HostEnvironment = CreateHostEnvironment()
TargetEnvironment = CreateTargetEnvironment(HostEnvironment)

Help(Vars.GenerateHelpText(HostEnvironment))

Export('HostEnvironment')
Export('TargetEnvironment')

VariantDir = f'build/{TargetEnvironment["build.arch"]}_{TargetEnvironment["build.config"]}'
BuildType = TargetEnvironment['build.type']

StageDir = os.path.abspath(os.path.join(VariantDir, 'img'))
RuntimeSysroot = StageDir

TargetEnvironment['ImageStagingDirectory'] = StageDir
TargetEnvironment['RuntimeDependencySysroot'] = RuntimeSysroot
TargetEnvironment['RuntimeDependencyMuslDirectory'] = RuntimeSysroot

RuntimeDependencies = None
if BuildType in ('full', 'usr', 'image'):
    Stamp = os.path.join(RuntimeSysroot, '.RuntimeDeps.stamp')
    RuntimeDependencies = HostEnvironment.Command(
        Stamp,
        ['scripts/base/dependencies.py'],
        BuildRuntimeDepsAction,
        TargetTriple=TargetEnvironment['TargetTriple'],
    )
    Export('RuntimeDependencies')

if BuildType in ('full', 'usr', 'image'):
    SConscript('usr/SConscript', variant_dir=f'{VariantDir}/usr', duplicate=0)

if BuildType in ('full', 'kernel', 'image'):
    SConscript('kernel/SConscript', variant_dir=f'{VariantDir}/kernel', duplicate=0)

if BuildType in ('full', 'bootloader', 'image'):
    SConscript('bootloader/Sconscript', variant_dir=f'{VariantDir}/bootloader', duplicate=0)

if BuildType in ('full', 'image'):
    SConscript('image/SConscript', variant_dir=VariantDir, duplicate=0)
