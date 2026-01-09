// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>

#include <fs/disk/partition.h>
#include <fs/fs_types.h>

typedef struct VFS_File
{
   Partition *partition; /* Resolved partition for this open file */
   FilesystemType type;  /* Filesystem implementation backing this node */
   void *fs_file;        /* Opaque FS-specific handle (e.g., FAT_File *) */
   bool is_directory;    /* Cached directory flag for quick checks */
   uint32_t size;        /* Size in bytes if known (0 for dirs/unknown) */
} VFS_File;

void VFS_Init(void);

int FS_Mount(Partition *volume, const char *location);
int FS_Umount(Partition *volume);

VFS_File *VFS_Open(const char *path);
uint32_t VFS_Read(VFS_File *file, uint32_t byteCount, void *dataOut);
uint32_t VFS_Write(VFS_File *file, uint32_t byteCount, const void *dataIn);
bool VFS_Seek(VFS_File *file, uint32_t position);
void VFS_Close(VFS_File *file);

uint32_t VFS_GetSize(VFS_File *file);

#endif