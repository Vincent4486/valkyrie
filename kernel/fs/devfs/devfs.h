// SPDX-License-Identifier: GPL-3.0-only

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
*/

#ifndef DEVFS_H
#define DEVFS_H

#include <fs/disk/disk.h>
#include <stdbool.h>
#include <stdint.h>
#include <valkyrie/fs.h>

// The reserved volume for DEVFS
#define DEVFS_VOLUME 30

// Maximum number of device nodes
#define DEVFS_MAXFILES 256

// Maximum path length for device names
#define DEVFS_PATHMAX 64

// Maximum data buffer size for in-memory devices
#define DEVFS_MAXDATA 4096

/* Device types for devfs */
typedef enum
{
   DEVFS_TYPE_BLOCK = 1, /* Block device (disks, partitions) */
   DEVFS_TYPE_CHAR = 2,  /* Character device (tty, null, zero) */
   DEVFS_TYPE_DIR = 3,   /* Directory node */
} DEVFS_DeviceType;

/* Forward declarations */
typedef struct DEVFS_File DEVFS_File;
typedef struct DEVFS_DeviceNode DEVFS_DeviceNode;

/* Device operation function pointers - drivers implement these */
typedef struct DEVFS_DeviceOps
{
   uint32_t (*read)(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                    void *buffer);
   uint32_t (*write)(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                     const void *buffer);
   int (*ioctl)(DEVFS_DeviceNode *node, uint32_t cmd, void *arg);
   void (*close)(DEVFS_DeviceNode *node);
} DEVFS_DeviceOps;

/* Device node - represents a registered device in devfs */
struct DEVFS_DeviceNode
{
   char name[DEVFS_PATHMAX];   /* Device name (e.g., "hda", "tty0") */
   DEVFS_DeviceType type;      /* Block, char, or directory */
   uint32_t major;             /* Major device number */
   uint32_t minor;             /* Minor device number */
   uint32_t size;              /* Size for block devices */
   const DEVFS_DeviceOps *ops; /* Device-specific operations */
   void *private_data;         /* Driver-specific data pointer */
   bool in_use;                /* True if this node is allocated */
};

/* Open file handle for devfs */
struct DEVFS_File
{
   DEVFS_DeviceNode *node; /* Reference to device node */
   uint32_t position;      /* Current read/write position */
   uint32_t flags;         /* Open flags */
};

/* Simple in-kernel device filesystem (devfs) interface. This provides a
 * minimal set of VFS-facing operations so the VFS can open/read/write
 * device nodes. Drivers register devices using DEVFS_RegisterDevice().
 *
 * Integration: devfs is initialized during FS_Initialize() on the reserved
 * volume slot (DEVFS_VOLUME = 30). After initialization, mount with:
 *   FS_Mount(&g_SysInfo->volume[DEVFS_VOLUME], "/dev");
 *
 * --note: DEVFS can only be single instance, located on Reserved volume 30--
 */

/* Initialize the devfs subsystem. Returns true on success.
 * This should be called from FS_Initialize() before disk scanning. */
bool DEVFS_Initialize(void);

/* Return pointer to VFS operations structure for devfs. */
const struct VFS_Operations *DEVFS_GetVFSOperations(void);

/* Get the devfs partition pointer for mounting */
Partition *DEVFS_GetPartition(void);

/*
 * Device Registration API - Drivers use these to add/remove device nodes
 */

/* Register a new device node in devfs.
 * @param name       Device name (e.g., "hda", "tty0", "null")
 * @param type       Device type (DEVFS_TYPE_BLOCK, DEVFS_TYPE_CHAR, etc.)
 * @param major      Major device number
 * @param minor      Minor device number
 * @param size       Size in bytes (for block devices)
 * @param ops        Device operation callbacks (can be NULL for basic devices)
 * @param private    Driver-specific data pointer
 * @return           Pointer to registered node, or NULL on failure
 */
DEVFS_DeviceNode *DEVFS_RegisterDevice(const char *name, DEVFS_DeviceType type,
                                       uint32_t major, uint32_t minor,
                                       uint32_t size,
                                       const DEVFS_DeviceOps *ops,
                                       void *private_data);

/* Unregister a device node from devfs.
 * @param node       Node to unregister (returned from DEVFS_RegisterDevice)
 * @return           true on success, false if node not found
 */
bool DEVFS_UnregisterDevice(DEVFS_DeviceNode *node);

/* Find a device node by name.
 * @param name       Device name to search for
 * @return           Pointer to node, or NULL if not found
 */
DEVFS_DeviceNode *DEVFS_FindDevice(const char *name);

/* List all registered devices (for debugging/enumeration).
 * @param callback   Function to call for each device
 * @param context    User context passed to callback
 */
typedef void (*DEVFS_EnumCallback)(DEVFS_DeviceNode *node, void *context);
void DEVFS_EnumerateDevices(DEVFS_EnumCallback callback, void *context);

/* Get number of registered devices */
uint32_t DEVFS_GetDeviceCount(void);

/*
 * File operations - used internally by VFS wrappers
 */

/* Open a device file */
DEVFS_File *DEVFS_Open(Partition *partition, const char *path);

/* Close a device file */
void DEVFS_Close(DEVFS_File *file);

/* Read from a device */
uint32_t DEVFS_Read(DEVFS_File *file, uint32_t byteCount, void *dataOut);

/* Write to a device */
uint32_t DEVFS_Write(DEVFS_File *file, uint32_t byteCount, const void *dataIn);

/* Seek in a device */
bool DEVFS_Seek(DEVFS_File *file, uint32_t position);

/* Get file size */
uint32_t DEVFS_GetSize(DEVFS_File *file);

#endif