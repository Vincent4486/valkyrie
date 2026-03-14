#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""
Helper script to bridge SCons configuration into Makefile environment.
Uses logic from scripts/scons/arch.py and scripts/scons/disk.py.
"""

import argparse
import sys
import os

# Add the workspace root to sys.path so we can import our scripts
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from scripts.scons.arch import get_arch_config, get_supported_archs
from scripts.scons.disk import get_supported_filesystems

def main():
    parser = argparse.ArgumentParser(description="SCons Configuration Helper for Makefile")
    parser.add_argument("--arch", required=True, help="Target architecture")
    parser.add_argument("--get", required=False, help="Configuration key to retrieve")
    parser.add_argument("--list-archs", action="store_true", help="List supported architectures")
    parser.add_argument("--list-filesystems", action="store_true", help="List supported filesystems")
    
    args = parser.parse_args()
    
    if args.list_archs:
        print(" ".join(get_supported_archs()))
        return
        
    if args.list_filesystems:
        print(" ".join(get_supported_filesystems()))
        return
        
    if args.get:
        config = get_arch_config(args.arch)
        if args.get in config:
            val = config[args.get]
            if isinstance(val, list):
                print(" ".join(val))
            else:
                print(val)
        else:
            sys.exit(1)

if __name__ == "__main__":
    main()
