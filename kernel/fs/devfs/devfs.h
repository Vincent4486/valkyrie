// SPDX-License-Identifier: GPL-3.0-only

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
*/

#ifndef DEVFS_H
#define DEVFS_H

#include <stdbool.h>
#include <stdint.h>
#include <valkyrie/fs.h>
#include <fs/disk/disk.h>

// The reserved volume for DEVFS
#define DEVFS_VOLUME 30

/* Simple in-kernel device filesystem (devfs) interface. This provides a
 * minimal set of VFS-facing operations so the VFS can open/read/write
 * device nodes. Implementations for specific devices will be added later.
 * 
 * Integration: devfs is automatically registered during DISK_Scan() as an
 * in-memory filesystem in a free volume slot (with disk=NULL). To mount:
 *   int devfs_idx = DISK_GetDevfsIndex();
 *   if (devfs_idx >= 0) FS_Mount(&g_SysInfo->volume[devfs_idx], "/dev");
 * 
 * --note: DEVFS can only be single instance, and located on Reserved volume 30--
 */

/* Initialize the devfs subsystem for a partition. Returns true on success. */
bool DEVFS_Initialize(Partition *partition);

/* Return pointer to VFS operations structure for devfs. */
const struct VFS_Operations *DEVFS_GetVFSOperations(void);

/* Convenience helpers for drivers: open/close a DEVFS_File directly. */
typedef struct DEVFS_File DEVFS_File;

DEVFS_File *DEVFS_Open(Partition *partition, const char *path);
void DEVFS_Close(DEVFS_File *file);

#endif