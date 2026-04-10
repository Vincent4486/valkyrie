# SPDX-License-Identifier: BSD-3-Clause

from decimal import Decimal
import re
import os

from SCons.Node.FS import Dir, File, Entry
from SCons.Environment import Environment

def ParseSize(size: str) -> int:
    SizeMatch = re.match(r'([0-9\.]+)([kmg]?)', size, re.IGNORECASE)
    if SizeMatch is None:
        raise ValueError(f'Error: Invalid size {size}')

    Result = Decimal(SizeMatch.group(1))
    Multiplier = SizeMatch.group(2).lower()

    Multipliers = {'k': 1024, 'm': 1024**2, 'g': 1024**3}
    if Multiplier in Multipliers:
        Result *= Multipliers[Multiplier]

    return int(Result)

def GlobRecursive(env: Environment, pattern: str, node: str = '.') -> list:
    Source = str(env.Dir(node).srcnode())
    WorkingDirectory = str(env.Dir('.').srcnode())

    DirectoryList = [Source]
    for Root, Directories, _ in os.walk(Source):
        for Directory in Directories:
            DirectoryList.append(os.path.join(Root, Directory))

    GlobResults = []
    for Directory in DirectoryList:
        Matched = env.Glob(os.path.join(os.path.relpath(Directory, WorkingDirectory), pattern))
        try:
            GlobResults.extend(list(Matched))
        except TypeError:
            GlobResults.append(Matched)

    return GlobResults


def GlobSources(srcpath: str, extensions: tuple = ('.c', '.cpp', '.S')) -> list:
    Sources = []
    for Root, _Directories, Files in os.walk(srcpath):
        for FileName in Files:
            if FileName.endswith(extensions):
                FullPath = os.path.join(Root, FileName)
                RelativePath = os.path.relpath(FullPath, srcpath)
                Sources.append(RelativePath)
    return Sources


def FindIndex(TheList: list, Predicate) -> int:
    for Index, Item in enumerate(TheList):
        if Predicate(Item):
            return Index
    return None


def IsFileName(obj, name: str) -> bool:
    if isinstance(obj, str):
        return name in obj
    elif isinstance(obj, (File, Dir, Entry)):
        return obj.name == name
    return False


def RemoveSuffix(s: str, suffix: str) -> str:
    if s.endswith(suffix):
        return s[:-len(suffix)]
    return s


def CreateBuildEnv(BaseEnvironment: Environment, srcpath: str, **KeywordArgs) -> Environment:
    EnvironmentObject = BaseEnvironment.Clone()
    
    EnvironmentObject.Append(
        CPATH=[srcpath],
        CPPPATH=[srcpath],
    )
    
    if KeywordArgs:
        EnvironmentObject.Append(**KeywordArgs)
    
    return EnvironmentObject