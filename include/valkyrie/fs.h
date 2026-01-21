// SPDX-License-Identifier: GPL-3.0-only

#ifndef FS_H
#define FS_H

#include <stdbool.h>
#include <stdint.h>

#include <fs/devfs/devfs.h>
#include <fs/disk/disk.h>
#include <fs/fd/fd.h>
#include <fs/misc/fs_types.h>
#include <fs/vfs/vfs.h>

/* Forward declaration to avoid circular dependency */
struct VFS_Operations;
typedef struct VFS_Operations VFS_Operations;

/* Filesystem device information */
typedef struct Filesystem
{
   FilesystemType type; /* Filesystem type (fat12, fat16, fat32, ext2, etc) */
   const VFS_Operations *ops; /* VFS operations for this filesystem */
   uint32_t block_size;       /* Block size in bytes */
   uint32_t total_blocks;     /* Total number of blocks */
   uint32_t used_blocks;      /* Used blocks */
   uint32_t free_blocks;      /* Free blocks */
   uint32_t inode_size;       /* Size of an inode */
   uint32_t total_inodes;     /* Total number of inodes */
   uint32_t free_inodes;      /* Free inodes */
   uint8_t mounted;           /* 1 if mounted, 0 otherwise */
   uint8_t read_only;         /* 1 if read-only, 0 if read-write */
} Filesystem;

bool FS_Initialize();

#endif