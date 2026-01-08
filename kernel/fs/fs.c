// SPDX-License-Identifier: AGPL-3.0-or-later

#include <fs/disk/disk.h>
#include <fs/disk/partition.h>
#include <fs/fat/fat.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stdint.h>
#include <sys/sys.h>

/**
 * Initialize storage system: scan and initialize all disks
 *
 * @return true on success, false on failure
 */
bool FS_Initialize()
{
   printf("[FS] Initializing filesystem\n");
   // Call DISK_Initialize to scan and populate all volumes
   int disksDetected = DISK_Initialize();
   if (disksDetected < 0)
   {
      printf("[FS] Disk initialization failed\n");
      return false;
   }
   printf("[FS] Filesystem initialization complete, disks detected: %d\n",
          disksDetected);
   return true;
}

/**
 * Mount a filesystem on a specific volume
 *
 * @param volume - Pointer to the volume to mount
 * @return filesystem index on success, -1 on failure
 */
int FS_Mount(Partition *volume, const char *location)
{
   printf("[FS] Mounting filesystem on volume at %s\n", volume->disk->brand);
   if (!volume || !volume->disk)
   {
      printf("[FS] Invalid volume\n");
      return -1;
   }

   // Check if filesystem is already initialized
   if (!volume->fs)
   {
      printf("[FS] No filesystem initialized on this volume\n");
      return -1;
   }

   // Mark filesystem as mounted
   volume->fs->mounted = 1;

   printf("[FS] Mounted filesystem on partition\n");
   return 0;
}

int FS_Umount(Partition *volume) { return -1; }
