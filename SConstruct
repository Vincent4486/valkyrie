# SPDX-License-Identifier: BSD-3-Clause
"""
Valecium OS Build System

Main build configuration file using SCons.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

from SCons.Action import Action
from SCons.Environment import Environment
from SCons.Script import AlwaysBuild, COMMAND_LINE_TARGETS, Exit, GetOption
from SCons.Variables import EnumVariable, Variables

from scripts.scons.arch import GetArchConfig, GetSupportedArchitectures
from scripts.scons.bootloader import (
    GetSupportedBootSystems,
    GetSupportedBootTypes,
    ShouldBuildSystemBootloader,
)
from scripts.scons.disk import GetSupportedFilesystems, GetSupportedPartitionMaps
from scripts.scons.utility import ParseSize

SetupOnlyTargets = {'deps', 'toolchain'}


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
        'BootSystem': 'grub',
        'BootType': 'bios',
        'DiskPartitionMap': 'mbr',
        'ToolchainPrefix': 'toolchain',
        'RunMemory': '4G',
        'RunSmp': '1',
        'GdbCommand': 'gdb',
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
    EnumVariable('BootSystem',
                 help='Boot system',
                 default='grub',
                 allowed_values=tuple(GetSupportedBootSystems())),
    EnumVariable('BootType',
                 help='Boot type',
                 default='bios',
                 allowed_values=tuple(GetSupportedBootTypes())),
    EnumVariable('DiskPartitionMap',
                 help='Disk partition map',
                 default='mbr',
                 allowed_values=tuple(GetSupportedPartitionMaps())),
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

Vars.Add('ToolchainPrefix',
         help='Cross-toolchain installation prefix',
         default='toolchain')

Vars.Add('RunMemory',
         help='Memory size passed to QEMU run/debug helpers',
         default='4G')

Vars.Add('RunSmp',
         help='CPU count passed to QEMU run helper',
         default='1')

Vars.Add('GdbCommand',
         help='GDB executable used by the debug helper',
         default='gdb')

Deps = {
    'binutils': '2.45',
    'gcc': '15.2.0',
}


def ResolvePath(PathValue: str) -> Path:
    Result = Path(str(PathValue)).expanduser()
    if not Result.is_absolute():
        Result = Path.cwd() / Result
    return Result.resolve()


def GetHomebrewPrefixes() -> list:
    Prefixes = []
    EnvPrefix = os.environ.get('HOMEBREW_PREFIX')
    if EnvPrefix:
        Prefixes.append(EnvPrefix)
    Prefixes.extend(['/opt/homebrew', '/usr/local'])

    UniquePrefixes = []
    for Prefix in Prefixes:
        if Prefix not in UniquePrefixes:
            UniquePrefixes.append(Prefix)
    return UniquePrefixes


def AddHostToolPaths(Env):
    for Prefix in reversed(GetHomebrewPrefixes()):
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'opt', 'e2fsprogs', 'sbin'))
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'opt', 'e2fsprogs', 'bin'))
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'opt', 'dosfstools', 'sbin'))
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'opt', 'make', 'libexec', 'gnubin'))
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'sbin'))
        Env.PrependENVPath('PATH', os.path.join(Prefix, 'bin'))


def ShouldUseRegularBuildTargets() -> bool:
    Targets = set(COMMAND_LINE_TARGETS)
    return not Targets or not Targets.issubset(SetupOnlyTargets)


def ShouldRequireTargetToolchain() -> bool:
    if GetOption('help') or GetOption('clean'):
        return False
    return ShouldUseRegularBuildTargets()


def CreateHostEnvironment():
    Env = Environment(
        variables=Vars,
        ENV=os.environ,
        CFLAGS=['-std=c99'],
        STRIP='strip',
    )

    AddHostToolPaths(Env)

    ToolchainPrefixPath = ResolvePath(Env['ToolchainPrefix'])
    Env['ToolchainPrefixPath'] = str(ToolchainPrefixPath)
    ToolchainBinPath = ToolchainPrefixPath / 'bin'
    if ToolchainBinPath.is_dir():
        Env.PrependENVPath('PATH', str(ToolchainBinPath))

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


def ResolveTools(Arch: str, ToolSearchPath: str):
    ArchitectureConfig = GetArchConfig(Arch)
    Prefixes = [
        ArchitectureConfig.get('ToolchainPrefix', ''),
        f'{Arch}-linux-musl-',
        f'{Arch}-elf-',
        '',
    ]

    UniquePrefixes = []
    for Prefix in Prefixes:
        if Prefix not in UniquePrefixes:
            UniquePrefixes.append(Prefix)

    Selected = ''
    for Prefix in UniquePrefixes:
        Gcc = f'{Prefix}gcc' if Prefix else 'gcc'
        if shutil.which(Gcc, path=ToolSearchPath):
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
    Missing = []

    for Key, Base in Bases.items():
        Preferred = f'{Selected}{Base}' if Selected else Base
        PreferredPath = shutil.which(Preferred, path=ToolSearchPath)
        Tools[Key] = Preferred
        Paths[Key] = PreferredPath or '<not found>'
        if Selected and not PreferredPath:
            Missing.append(Preferred)

    if Missing:
        MissingList = ', '.join(Missing)
        print(f"Error: Cross-toolchain for {Arch} is incomplete. Missing: {MissingList}")
        Exit(1)

    return Tools, Paths, Selected


def CreateTargetEnvironment(HostEnv):
    Arch = HostEnv['BuildArch']
    ArchitectureConfig = GetArchConfig(Arch)

    Tools, ToolPaths, Prefix = ResolveTools(Arch, HostEnv['ENV'].get('PATH', os.environ['PATH']))
    if ShouldRequireTargetToolchain() and not Prefix:
        ExpectedPrefix = ArchitectureConfig.get('ToolchainPrefix', f'{Arch}-linux-musl-')
        print(
            f"Error: No cross-toolchain found for {Arch}. Expected {ExpectedPrefix}gcc "
            f"on PATH or under {HostEnv['ToolchainPrefixPath']}/bin. "
            "Run `scons toolchain` first, or set ToolchainPrefix=/path/to/toolchain."
        )
        Exit(1)

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


def GetImageType(Env):
    return 'cdrom' if Env['ImageFormat'] == 'iso' else 'disk'


def RunSubprocess(Command, Env):
    return subprocess.call(Command, env=Env['ENV'])


def RunDependenciesAction(target, source, env):
    _ = (target, source)
    Script = os.path.join('scripts', 'base', 'dependencies.py')
    return RunSubprocess([sys.executable, Script, '-y'], env)


def RunToolchainAction(target, source, env):
    _ = (target, source)
    Script = os.path.join('scripts', 'base', 'toolchain.py')
    Command = [
        sys.executable,
        Script,
        env['ToolchainPrefixPath'],
        '-a',
        env['BuildArch'],
        '--ensure',
    ]
    return RunSubprocess(Command, env)


def RunQemuAction(target, source, env):
    _ = target
    ImagePath = str(source[0])
    Command = [
        sys.executable,
        os.path.join('scripts', 'base', 'qemu.py'),
        GetImageType(env),
        ImagePath,
        '-a',
        env['BuildArch'],
        '-m',
        env['RunMemory'],
        '-s',
        str(env['RunSmp']),
    ]
    return RunSubprocess(Command, env)


def RunDebugAction(target, source, env):
    _ = target
    ImagePath = str(source[0])
    KernelPath = str(source[1])
    Command = [
        sys.executable,
        os.path.join('scripts', 'base', 'gdb.py'),
        GetImageType(env),
        ImagePath,
        KernelPath,
        '-a',
        env['BuildArch'],
        '-m',
        env['RunMemory'],
        '--gdb',
        env['GdbCommand'],
    ]
    return RunSubprocess(Command, env)


def FailRunAction(target, source, env):
    _ = (target, source)
    print(
        "The `run` target requires BuildType=full or BuildType=image "
        f"(current BuildType={env['BuildType']})."
    )
    return 1


def FailDebugAction(target, source, env):
    _ = (target, source)
    print(
        "The `debug` target requires BuildType=full or BuildType=image "
        f"(current BuildType={env['BuildType']})."
    )
    return 1


def RegisterPhonyTargets(Env, VariantDir):
    DependenciesAlias = Env.Alias(
        'deps',
        [],
        Action(RunDependenciesAction, '   DEPS    host packages'),
    )
    ToolchainAlias = Env.Alias(
        'toolchain',
        [],
        Action(RunToolchainAction, '   TOOLCHAIN $ToolchainPrefixPath'),
    )
    AlwaysBuild(DependenciesAlias)
    AlwaysBuild(ToolchainAlias)

    ImageOutputName = (
        f'{Env["ImageName"]}-{Env["ProjVersion"]}_'
        f'{Env["BuildConfig"]}_{Env["BuildArch"]}.{Env["ImageFormat"]}'
    )
    ImageNode = Env.File(os.path.join(VariantDir, ImageOutputName))
    KernelNode = Env.File(os.path.join(VariantDir, 'kernel', Env['KernelOutputName']))

    if Env['BuildType'] in ('full', 'image'):
        RunAlias = Env.Alias('run', [ImageNode], Action(RunQemuAction, '   RUN     $SOURCE'))
        DebugAlias = Env.Alias(
            'debug',
            [ImageNode, KernelNode],
            Action(RunDebugAction, '   DEBUG   $SOURCES'),
        )
    else:
        RunAlias = Env.Alias('run', [], Action(FailRunAction, '   RUN     unavailable'))
        DebugAlias = Env.Alias('debug', [], Action(FailDebugAction, '   DEBUG   unavailable'))

    AlwaysBuild(RunAlias)
    AlwaysBuild(DebugAlias)


HostEnvironment = CreateHostEnvironment()
TargetEnvironment = CreateTargetEnvironment(HostEnvironment)

Help(Vars.GenerateHelpText(HostEnvironment))

Export('HostEnvironment')
Export('TargetEnvironment')

VariantDir = f'build/{TargetEnvironment["BuildArch"]}_{TargetEnvironment["BuildConfig"]}'
BuildType = TargetEnvironment['BuildType']
BootSystem = TargetEnvironment['BootSystem']

StageDir = os.path.abspath(os.path.join(VariantDir, 'img'))

TargetEnvironment['ImageStagingDirectory'] = StageDir
TargetEnvironment['BootloaderComponents'] = {}

RegisterPhonyTargets(TargetEnvironment, VariantDir)

if ShouldUseRegularBuildTargets() and BuildType in ('full', 'usr', 'image'):
    SConscript('usr/SConscript', variant_dir=f'{VariantDir}/usr', duplicate=0)

if ShouldUseRegularBuildTargets() and BuildType in ('full', 'kernel', 'image'):
    SConscript('kernel/SConscript', variant_dir=f'{VariantDir}/kernel', duplicate=0)

if (
    ShouldUseRegularBuildTargets()
    and BuildType in ('full', 'bootloader', 'image')
    and ShouldBuildSystemBootloader(BootSystem)
):
    SConscript('bootloader/Sconscript', variant_dir=f'{VariantDir}/bootloader', duplicate=0)

if ShouldUseRegularBuildTargets() and BuildType in ('full', 'image'):
    SConscript('image/SConscript', variant_dir=VariantDir, duplicate=0)
