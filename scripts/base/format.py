#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Code formatter for Valkyrie OS.

Formats all C, C++, and header files using clang-format.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


# File extensions to format
SOURCE_EXTENSIONS = {'.c', '.cpp', '.cc', '.cxx', '.h', '.hpp', '.hxx'}

# Directories to skip
SKIP_DIRS = {'build', 'toolchain', '.git', '__pycache__', 'node_modules'}


def find_source_files(root_dir: str, extensions: set = None) -> list:
    """Find all source files in a directory tree.
    
    Args:
        root_dir: Root directory to search
        extensions: Set of file extensions to include
    
    Returns:
        List of file paths
    """
    extensions = extensions or SOURCE_EXTENSIONS
    files = []
    
    for dirpath, dirnames, filenames in os.walk(root_dir):
        # Remove directories to skip
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        
        for filename in filenames:
            ext = Path(filename).suffix.lower()
            if ext in extensions:
                files.append(os.path.join(dirpath, filename))
    
    return files


def format_files(files: list, formatter: str = 'clang-format',
                 check_only: bool = False, verbose: bool = False) -> int:
    """Format source files using clang-format.
    
    Args:
        files: List of file paths to format
        formatter: Formatter command name
        check_only: If True, only check formatting without modifying
        verbose: Print each file being processed
    
    Returns:
        0 if successful, 1 if changes needed (check_only) or errors
    """
    if not files:
        print("No source files found.")
        return 0
    
    # Build command
    cmd = [formatter]
    if check_only:
        cmd.extend(['--dry-run', '--Werror'])
    else:
        cmd.append('-i')
    
    errors = 0
    
    for filepath in files:
        if verbose:
            action = "Checking" if check_only else "Formatting"
            print(f"{action}: {filepath}")
        
        result = subprocess.run(cmd + [filepath], capture_output=True)
        if result.returncode != 0:
            errors += 1
            if check_only:
                print(f"Needs formatting: {filepath}")
            else:
                print(f"Error formatting: {filepath}")
                if result.stderr:
                    print(result.stderr.decode())
    
    if check_only:
        if errors > 0:
            print(f"\n{errors} file(s) need formatting.")
            return 1
        else:
            print("All files properly formatted.")
            return 0
    else:
        if errors > 0:
            print(f"\n{errors} file(s) had errors.")
            return 1
        else:
            print(f"Formatted {len(files)} file(s).")
            return 0


def main():
    parser = argparse.ArgumentParser(
        description='Format C/C++ source files in the Valkyrie OS project',
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    
    parser.add_argument('paths', nargs='*', default=['.'],
                        help='Directories or files to format (default: current directory)')
    parser.add_argument('-c', '--check', action='store_true',
                        help='Check formatting without modifying files')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Print each file being processed')
    parser.add_argument('--formatter', default='clang-format',
                        help='Formatter command (default: clang-format)')
    
    args = parser.parse_args()
    
    # Collect all files to format
    all_files = []
    for path in args.paths:
        if os.path.isfile(path):
            all_files.append(path)
        elif os.path.isdir(path):
            all_files.extend(find_source_files(path))
        else:
            print(f"Warning: Path not found: {path}", file=sys.stderr)
    
    if not all_files:
        print("No files to format.")
        sys.exit(0)
    
    sys.exit(format_files(
        files=all_files,
        formatter=args.formatter,
        check_only=args.check,
        verbose=args.verbose,
    ))


if __name__ == '__main__':
    main()
