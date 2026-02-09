// SPDX-License-Identifier: GPL-3.0-only

#include "devfs.h"
#include <drivers/tty/tty.h>
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <sys/sys.h>

/*
 * In-memory device filesystem (devfs) implementation
 * 
 * This provides a virtual filesystem for device nodes that drivers can
 * register and unregister dynamically. The filesystem exists entirely
 * in memory and is initialized on the reserved volume slot (DEVFS_VOLUME).
 */

/* Global device node table */
static DEVFS_DeviceNode g_DevNodes[DEVFS_MAXFILES];
static uint32_t g_DevNodeCount = 0;
static bool g_DevfsInitialized = false;

/* Devfs partition and filesystem structures (in-memory) */
static Partition g_DevfsPartition;
static Filesystem g_DevfsFilesystem;

/* Forward declarations for VFS operations */
static VFS_File *devfs_vfs_open(Partition *partition, const char *path);
static uint32_t devfs_vfs_read(Partition *partition, void *fs_file,
                               uint32_t byteCount, void *dataOut);
static uint32_t devfs_vfs_write(Partition *partition, void *fs_file,
                                uint32_t byteCount, const void *dataIn);
static bool devfs_vfs_seek(Partition *partition, void *fs_file, uint32_t pos);
static void devfs_vfs_close(void *fs_file);
static uint32_t devfs_vfs_get_size(void *fs_file);
static bool devfs_vfs_delete(Partition *partition, const char *path);

/* VFS operations table */
static const VFS_Operations devfs_ops = {
   .open = devfs_vfs_open,
   .read = devfs_vfs_read,
   .write = devfs_vfs_write,
   .seek = devfs_vfs_seek,
   .close = devfs_vfs_close,
   .get_size = devfs_vfs_get_size,
   .delete = devfs_vfs_delete
};

/*
 * Helper functions
 */

/* Normalize a device path - strip leading /dev/ or / if present */
static const char *normalize_device_path(const char *path)
{
   if (!path) return NULL;
   
   /* Skip leading slashes */
   while (*path == '/') path++;
   
   /* Skip "dev/" prefix if present */
   if (strncmp(path, "dev/", 4) == 0) {
      path += 4;
   }
   
   return path;
}

/*
 * Device Registration API Implementation
 */

DEVFS_DeviceNode *DEVFS_RegisterDevice(const char *name, DEVFS_DeviceType type,
                                       uint32_t major, uint32_t minor,
                                       uint32_t size, const DEVFS_DeviceOps *ops,
                                       void *private_data)
{
   if (!name || name[0] == '\0') {
      printf("[DEVFS] RegisterDevice: invalid name\n");
      return NULL;
   }
   
   if (g_DevNodeCount >= DEVFS_MAXFILES) {
      printf("[DEVFS] RegisterDevice: device table full\n");
      return NULL;
   }
   
   /* Check for duplicate name */
   for (uint32_t i = 0; i < DEVFS_MAXFILES; i++) {
      if (g_DevNodes[i].in_use && 
          strncmp(g_DevNodes[i].name, name, DEVFS_PATHMAX) == 0) {
         printf("[DEVFS] RegisterDevice: '%s' already exists\n", name);
         return NULL;
      }
   }
   
   /* Find a free slot */
   DEVFS_DeviceNode *node = NULL;
   for (uint32_t i = 0; i < DEVFS_MAXFILES; i++) {
      if (!g_DevNodes[i].in_use) {
         node = &g_DevNodes[i];
         break;
      }
   }
   
   if (!node) {
      printf("[DEVFS] RegisterDevice: no free slots\n");
      return NULL;
   }
   
   /* Initialize the node */
   memset(node, 0, sizeof(DEVFS_DeviceNode));
   strncpy(node->name, name, DEVFS_PATHMAX - 1);
   node->name[DEVFS_PATHMAX - 1] = '\0';
   node->type = type;
   node->major = major;
   node->minor = minor;
   node->size = size;
   node->ops = ops;
   node->private_data = private_data;
   node->in_use = true;
   
   g_DevNodeCount++;
   
   logfmt(LOG_INFO, "[DEVFS] Registered device: %s (type=%d, major=%u, minor=%u)\n",
          name, type, major, minor);
   
   return node;
}

bool DEVFS_UnregisterDevice(DEVFS_DeviceNode *node)
{
   if (!node) return false;
   
   /* Verify node is in our table */
   bool found = false;
   for (uint32_t i = 0; i < DEVFS_MAXFILES; i++) {
      if (&g_DevNodes[i] == node && node->in_use) {
         found = true;
         break;
      }
   }
   
   if (!found) {
      printf("[DEVFS] UnregisterDevice: node not found\n");
      return false;
   }
   
   logfmt(LOG_INFO, "[DEVFS] Unregistered device: %s\n", node->name);
   
   /* Clear the node */
   memset(node, 0, sizeof(DEVFS_DeviceNode));
   node->in_use = false;
   g_DevNodeCount--;
   
   return true;
}

DEVFS_DeviceNode *DEVFS_FindDevice(const char *name)
{
   if (!name) return NULL;
   
   const char *normalized = normalize_device_path(name);
   if (!normalized) return NULL;
   
   for (uint32_t i = 0; i < DEVFS_MAXFILES; i++) {
      if (g_DevNodes[i].in_use &&
          strncmp(g_DevNodes[i].name, normalized, DEVFS_PATHMAX) == 0) {
         return &g_DevNodes[i];
      }
   }
   
   return NULL;
}

void DEVFS_EnumerateDevices(DEVFS_EnumCallback callback, void *context)
{
   if (!callback) return;
   
   for (uint32_t i = 0; i < DEVFS_MAXFILES; i++) {
      if (g_DevNodes[i].in_use) {
         callback(&g_DevNodes[i], context);
      }
   }
}

uint32_t DEVFS_GetDeviceCount(void)
{
   return g_DevNodeCount;
}

/*
 * External standard device implementations (defined in fs/misc/std_dev.c)
 */
extern void register_standard_devices(void);

/*
 * File operations implementation
 */

DEVFS_File *DEVFS_Open(Partition *partition, const char *path)
{
   (void)partition; /* Devfs doesn't use partition for lookups */
   
   if (!path) return NULL;
   
   DEVFS_DeviceNode *node = DEVFS_FindDevice(path);
   if (!node) {
      logfmt(LOG_WARNING, "[DEVFS] Open: device '%s' not found\n", path);
      return NULL;
   }
   
   DEVFS_File *file = kmalloc(sizeof(DEVFS_File));
   if (!file) return NULL;
   
   memset(file, 0, sizeof(DEVFS_File));
   file->node = node;
   file->position = 0;
   file->flags = 0;
   
   return file;
}

void DEVFS_Close(DEVFS_File *file)
{
   if (!file) return;
   
   /* Call device-specific close if provided */
   if (file->node && file->node->ops && file->node->ops->close) {
      file->node->ops->close(file->node);
   }
   
   free(file);
}

uint32_t DEVFS_Read(DEVFS_File *file, uint32_t byteCount, void *dataOut)
{
   if (!file || !file->node || !dataOut || byteCount == 0) return 0;
   
   /* Use device-specific read if available */
   if (file->node->ops && file->node->ops->read) {
      uint32_t bytes_read = file->node->ops->read(file->node, file->position,
                                                   byteCount, dataOut);
      file->position += bytes_read;
      return bytes_read;
   }
   
   /* No read operation - return 0 */
   return 0;
}

uint32_t DEVFS_Write(DEVFS_File *file, uint32_t byteCount, const void *dataIn)
{
   if (!file || !file->node || !dataIn || byteCount == 0) return 0;
   
   /* Use device-specific write if available */
   if (file->node->ops && file->node->ops->write) {
      uint32_t bytes_written = file->node->ops->write(file->node, file->position,
                                                       byteCount, dataIn);
      file->position += bytes_written;
      return bytes_written;
   }
   
   /* No write operation - return 0 */
   return 0;
}

bool DEVFS_Seek(DEVFS_File *file, uint32_t position)
{
   if (!file) return false;
   
   file->position = position;
   return true;
}

uint32_t DEVFS_GetSize(DEVFS_File *file)
{
   if (!file || !file->node) return 0;
   return file->node->size;
}

/*
 * VFS wrapper functions
 */

static VFS_File *devfs_vfs_open(Partition *partition, const char *path)
{
   DEVFS_File *df = DEVFS_Open(partition, path);
   if (!df) return NULL;
   
   VFS_File *vf = kmalloc(sizeof(VFS_File));
   if (!vf) {
      DEVFS_Close(df);
      return NULL;
   }
   
   memset(vf, 0, sizeof(VFS_File));
   vf->partition = partition;
   vf->type = DEVFS;
   vf->fs_file = df;
   vf->is_directory = (df->node->type == DEVFS_TYPE_DIR);
   vf->size = df->node->size;
   
   logfmt(LOG_INFO, "[DEVFS] Opened device: %s\n", path);
   return vf;
}

static uint32_t devfs_vfs_read(Partition *partition, void *fs_file,
                               uint32_t byteCount, void *dataOut)
{
   (void)partition;
   if (!fs_file || !dataOut || byteCount == 0) return 0;
   
   return DEVFS_Read((DEVFS_File *)fs_file, byteCount, dataOut);
}

static uint32_t devfs_vfs_write(Partition *partition, void *fs_file,
                                uint32_t byteCount, const void *dataIn)
{
   (void)partition;
   if (!fs_file || !dataIn || byteCount == 0) return 0;
   
   return DEVFS_Write((DEVFS_File *)fs_file, byteCount, dataIn);
}

static bool devfs_vfs_seek(Partition *partition, void *fs_file, uint32_t pos)
{
   (void)partition;
   if (!fs_file) return false;
   
   return DEVFS_Seek((DEVFS_File *)fs_file, pos);
}

static void devfs_vfs_close(void *fs_file)
{
   DEVFS_Close((DEVFS_File *)fs_file);
}

static uint32_t devfs_vfs_get_size(void *fs_file)
{
   if (!fs_file) return 0;
   return DEVFS_GetSize((DEVFS_File *)fs_file);
}

static bool devfs_vfs_delete(Partition *partition, const char *path)
{
   (void)partition;
   (void)path;
   /* Device nodes cannot be deleted via VFS - use DEVFS_UnregisterDevice */
   return false;
}

/*
 * Initialization
 */

bool DEVFS_Initialize(void)
{
   if (g_DevfsInitialized) {
      logfmt(LOG_WARNING, "[DEVFS] Already initialized\n");
      return true;
   }
   
   /* Clear device node table */
   memset(g_DevNodes, 0, sizeof(g_DevNodes));
   g_DevNodeCount = 0;
   
   /* Initialize the in-memory filesystem structure */
   memset(&g_DevfsFilesystem, 0, sizeof(Filesystem));
   g_DevfsFilesystem.type = DEVFS;
   g_DevfsFilesystem.ops = &devfs_ops;
   g_DevfsFilesystem.mounted = 0;
   g_DevfsFilesystem.read_only = 0;
   g_DevfsFilesystem.block_size = 0; /* No block device backing */
   
   /* Initialize the in-memory partition structure */
   memset(&g_DevfsPartition, 0, sizeof(Partition));
   g_DevfsPartition.disk = NULL;  /* No backing disk */
   g_DevfsPartition.partitionOffset = 0;
   g_DevfsPartition.partitionSize = 0;
   g_DevfsPartition.partitionType = 0;
   g_DevfsPartition.fs = &g_DevfsFilesystem;
   g_DevfsPartition.uuid = 0xDEADBEEF; /* Marker UUID for devfs */
   strncpy(g_DevfsPartition.label, "devfs", sizeof(g_DevfsPartition.label) - 1);
   g_DevfsPartition.isRootPartition = false;
   
   /* Place devfs partition in the reserved volume slot */
   g_SysInfo->volume[DEVFS_VOLUME] = g_DevfsPartition;
   g_SysInfo->volume[DEVFS_VOLUME].fs = &g_DevfsFilesystem;
   
   /* Register standard devices */
   register_standard_devices();
   
   g_DevfsInitialized = true;
   logfmt(LOG_INFO, "[DEVFS] Initialized on volume[%d]\n", DEVFS_VOLUME);
   
   return true;
}

const VFS_Operations *DEVFS_GetVFSOperations(void)
{
   return &devfs_ops;
}

Partition *DEVFS_GetPartition(void)
{
   if (!g_DevfsInitialized) return NULL;
   return &g_SysInfo->volume[DEVFS_VOLUME];
}
