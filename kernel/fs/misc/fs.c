// SPDX-License-Identifier: GPL-3.0-only

#include <fs/devfs/devfs.h>
#include <fs/fat/fat.h>
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stdint.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>

/**
 * Initialize the devfs filesystem on reserved volume slot
 * This creates an in-memory filesystem for device nodes.
 */
static void InitializeDevfs(void)
{
   if (!DEVFS_Initialize())
   {
      logfmt(LOG_ERROR, "[FS] Failed to initialize devfs\n");
      return;
   }

   /* Mount devfs at /dev */
   Partition *devfs_part = DEVFS_GetPartition();
   if (devfs_part)
   {
      FS_Mount(devfs_part, "/dev");
   }
}

/**
 * Initialize storage system: scan and initialize all disks
 *
 * @return true on success, false on failure
 */
bool FS_Initialize()
{
   VFS_Init();

   /* Initialize devfs first - this sets up the device filesystem
    * on the reserved volume slot (DEVFS_VOLUME = 30) and mounts
    * it at /dev. Drivers will register their devices during
    * DISK_Initialize(). */
   InitializeDevfs();

   /* Call DISK_Initialize to scan and populate all volumes.
    * This will also trigger drivers to register their devices
    * in devfs during the scan process. */
   int disksDetected = DISK_Initialize();
   if (disksDetected < 0)
   {
      logfmt(LOG_ERROR, "[FS] Disk initialization failed\n");
      return false;
   }
   return true;
}
