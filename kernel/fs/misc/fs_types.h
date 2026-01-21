// SPDX-License-Identifier: GPL-3.0-only

#ifndef FS_TYPES_H
#define FS_TYPES_H

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
*/

/* Common filesystem enumerations shared between VFS and FS interfaces. */
typedef enum FilesystemType
{
   FAT12 = 1,
   FAT16 = 2,
   FAT32 = 3,
   EXT2 = 4
} FilesystemType;

#endif
