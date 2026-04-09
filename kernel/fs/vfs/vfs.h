// SPDX-License-Identifier: GPL-3.0-only

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
*/

#ifndef VFS_H
#define VFS_H

#include <stdbool.h>
#include <stdint.h>

#include <fs/disk/disk.h>
#include <fs/misc/fs_types.h>

/* Forward declarations */
struct VFS_File;
typedef struct VFS_File VFS_File;
struct VFS_DirEntry;
typedef struct VFS_DirEntry VFS_DirEntry;
struct Partition;
typedef struct Partition Partition;

#define VFS_ACCESS_EXEC 0x1u
#define VFS_ACCESS_WRITE 0x2u
#define VFS_ACCESS_READ 0x4u

#define VFS_OK 0
#define VFS_EINVAL (-1)
#define VFS_ENOENT (-2)
#define VFS_EIO (-3)
#define VFS_EPERM (-4)
#define VFS_ENOTSUP (-5)
#define VFS_EOF (-6)

/* VFS operations structure - Linux-style function pointers */
typedef struct VFS_Operations
{
   VFS_File *(*open)(Partition *partition, const char *path);
   VFS_File *(*create)(Partition *partition, const char *path, uint16_t mode);
   int (*readdir)(Partition *partition, void *fs_file, VFS_DirEntry *entryOut);
   uint32_t (*read)(Partition *partition, void *fs_file, uint32_t byteCount,
                    void *dataOut);
   uint32_t (*write)(Partition *partition, void *fs_file, uint32_t byteCount,
                     const void *dataIn);
   int (*seek)(Partition *partition, void *fs_file, uint32_t position);
   void (*close)(void *fs_file);
   uint32_t (*get_size)(void *fs_file);
   int (*delete)(Partition *partition, const char *path);
   int (*access)(Partition *partition, const char *path, uint32_t uid,
                 uint32_t gid, uint8_t accessMask);
   int (*chmod)(Partition *partition, const char *path, uint16_t mode);
   int (*chown)(Partition *partition, const char *path, uint32_t uid,
                uint32_t gid);
} VFS_Operations;

typedef struct VFS_File
{
   Partition *partition; /* Resolved partition for this open file */
   FilesystemType type;  /* Filesystem implementation backing this node */
   void *fs_file;        /* Opaque FS-specific handle (e.g., FAT_File *) */
   bool is_directory;    /* Cached directory flag for quick checks */
   uint32_t size;        /* Size in bytes if known (0 for dirs/unknown) */
} VFS_File;

typedef struct VFS_DirEntry
{
   char name[32];
   bool is_directory;
   uint32_t size;
} VFS_DirEntry;

void VFS_Init(void);

int FS_Mount(Partition *volume, const char *location);
int FS_Umount(Partition *volume);

VFS_File *VFS_Open(const char *path);
VFS_File *VFS_OpenDir(const char *path);
VFS_File *VFS_Create(const char *path, uint16_t mode);
int VFS_ReadDir(VFS_File *directory, VFS_DirEntry *entryOut);
uint32_t VFS_Read(VFS_File *file, uint32_t byteCount, void *dataOut);
uint32_t VFS_Write(VFS_File *file, uint32_t byteCount, const void *dataIn);
int VFS_Seek(VFS_File *file, uint32_t position);
void VFS_Close(VFS_File *file);
int VFS_Delete(const char *path);
int VFS_Access(const char *path, uint32_t uid, uint32_t gid,
               uint8_t accessMask);
int VFS_Chmod(const char *path, uint16_t mode);
int VFS_Chown(const char *path, uint32_t uid, uint32_t gid);

uint32_t VFS_GetSize(VFS_File *file);

void VFS_SelfTest(void);

#endif