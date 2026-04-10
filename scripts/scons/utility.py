# SPDX-License-Identifier: BSD-3-Clause

from decimal import Decimal
import re
import os

from SCons.Node.FS import Dir, File, Entry
from SCons.Environment import Environment

def ParseSize(size: str) -> int:
    size_match = re.match(r'([0-9\.]+)([kmg]?)', size, re.IGNORECASE)
    if size_match is None:
        raise ValueError(f'Error: Invalid size {size}')

    result = Decimal(size_match.group(1))
    multiplier = size_match.group(2).lower()

    multipliers = {'k': 1024, 'm': 1024**2, 'g': 1024**3}
    if multiplier in multipliers:
        result *= multipliers[multiplier]

    return int(result)

def GlobRecursive(env: Environment, pattern: str, node: str = '.') -> list:
    src = str(env.Dir(node).srcnode())
    cwd = str(env.Dir('.').srcnode())

    dir_list = [src]
    for root, directories, _ in os.walk(src):
        for d in directories:
            dir_list.append(os.path.join(root, d))

    globs = []
    for d in dir_list:
        matched = env.Glob(os.path.join(os.path.relpath(d, cwd), pattern))
        try:
            globs.extend(list(matched))
        except TypeError:
            globs.append(matched)

    return globs


def GlobSources(srcpath: str, extensions: tuple = ('.c', '.cpp', '.S')) -> list:
    sources = []
    for root, dirs, files in os.walk(srcpath):
        for file in files:
            if file.endswith(extensions):
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, srcpath)
                sources.append(rel_path)
    return sources


def FindIndex(the_list: list, predicate) -> int:
    for i, item in enumerate(the_list):
        if predicate(item):
            return i
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


def CreateBuildEnv(base_env: Environment, srcpath: str, **kwargs) -> Environment:
    env = base_env.Clone()
    
    # Set up include paths
    env.Append(
        CPATH=[srcpath],
        CPPPATH=[srcpath],
    )
    
    # Apply any additional settings
    if kwargs:
        env.Append(**kwargs)
    
    return env