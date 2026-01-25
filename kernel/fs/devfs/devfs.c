// SPDX-License-Identifier: GPL-3.0-only

#include "devfs.h"
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>

#define DEVFS_MAXFILES 256
#define DEVFS_PATHMAX 64

typedef struct DEVFS_FileOperations{
	// to be implemented
} DEVFS_FileOperations;

typedef struct DEVFS_File
{
   // needs modification
   uint16_t type;
   char path[DEVFS_PATHMAX];
   bool is_device;
   uint32_t position;
   uint32_t size;
   const DEVFS_FileOperations ops;
   void *private; /* device-specific pointer */
} DEVFS_File;

static DEVFS_File *g_DevFiles[DEVFS_MAXFILES];

bool DEVFS_Register(DEVFS_File *file){
	// to be implemented
	return false;
}

bool DEVFS_Unregister(DEVFS_File *file){
	// to be implemented
	return false;
}

/* Basic no-op implementations: real devices should register handlers. */
/* Driver-facing: open a DEVFS_File directly */
DEVFS_File *DEVFS_Open(Partition *partition, const char *path)
{
	if (!path) return NULL;
	DEVFS_File *df = kmalloc(sizeof(DEVFS_File));
	if (!df) return NULL;
	memset(df, 0, sizeof(DEVFS_File));
	df->is_device = true;
	df->position = 0;
	df->size = 0;
	strncpy(df->path, path, sizeof(df->path) - 1);
	return df;
}

uint32_t DEVFS_Write(Partition *disk, DEVFS_File *file, uint32_t byteCount, void *dataIn){
	// to be implemented
	return 0;
}

uint32_t DEVFS_Read(Partition *disk, DEVFS_File *file, uint32_t byteCount, void *dataOut){
	// to be implemented
	return 0;
}

bool DEVFS_Seek(Partition *disk, DEVFS_File *file, uint32_t pos){
	// to be implemented
	return false;
}

void DEVFS_Close(DEVFS_File *fs_file){
	// to be implemented
	(void)fs_file;
}

/* VFS-facing wrapper that returns a VFS_File containing a DEVFS_File */
static VFS_File *devfs_vfs_open(Partition *partition, const char *path)
{
	DEVFS_File *df = DEVFS_Open(partition, path);
	if (!df) return NULL;

	VFS_File *vf = kmalloc(sizeof(VFS_File));
	if (!vf)
	{
		DEVFS_Close(df);
		return NULL;
	}

	memset(vf, 0, sizeof(VFS_File));
	vf->partition = partition;
	vf->type = DEVFS;
	vf->fs_file = df;
	vf->is_directory = false;
	vf->size = df->size;
    printf("Reading a DEVFS file: %s \n", path);
	return vf;
}

static uint32_t devfs_vfs_read(Partition *partition, void *fs_file,
										 uint32_t byteCount, void *dataOut)
{
	(void)partition;
	if (!fs_file || !dataOut || byteCount == 0) return 0;

	/* No generic device read implemented yet. Return 0 bytes. */
	return 0;
}

static uint32_t devfs_vfs_write(Partition *partition, void *fs_file,
										  uint32_t byteCount, const void *dataIn)
{
	(void)partition;
	if (!fs_file || !dataIn || byteCount == 0) return 0;

	/* No generic device write implemented yet. Return 0 bytes written. */
	return 0;
}

static bool devfs_vfs_seek(Partition *partition, void *fs_file, uint32_t pos)
{
	(void)partition;
	if (!fs_file) return false;
	DEVFS_File *df = (DEVFS_File *)fs_file;
	df->position = pos;
	return true;
}

static void devfs_vfs_close(void *fs_file)
{
	DEVFS_Close((DEVFS_File *)fs_file);
}

static uint32_t devfs_vfs_get_size(void *fs_file)
{
	if (!fs_file) return 0;
	DEVFS_File *df = (DEVFS_File *)fs_file;
	return df->size;
}

static bool devfs_vfs_delete(Partition *partition, const char *path)
{
	(void)partition;
	(void)path;
	/* devfs nodes are not removable via VFS delete API */
	return false;
}

static const VFS_Operations devfs_ops = {
	 .open = devfs_vfs_open,
	 .read = devfs_vfs_read,
	 .write = devfs_vfs_write,
	 .seek = devfs_vfs_seek,
	 .close = devfs_vfs_close,
	 .get_size = devfs_vfs_get_size,
	 .delete = devfs_vfs_delete};

bool DEVFS_Initialize(Partition *partition)
{
	if (!partition) return false;
	if (!partition->fs) return false;
	partition->fs->ops = &devfs_ops;
	partition->fs->mounted = 1;
	memset(g_DevFiles, 0, sizeof(g_DevFiles));
	return true;
}

const struct VFS_Operations *DEVFS_GetVFSOperations(void) { return &devfs_ops; }
