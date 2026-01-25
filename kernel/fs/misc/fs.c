// SPDX-License-Identifier: GPL-3.0-only

#include <fs/fat/fat.h>
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stdint.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>

static void RegisterDevfs(){
   // to be implemented
   /*
      Todo:
      Create a partition and initialize the devfs
   */
}

/**
 * Initialize storage system: scan and initialize all disks
 *
 * @return true on success, false on failure
 */
bool FS_Initialize()
{
   VFS_Init();
   RegisterDevfs();

   // Call DISK_Initialize to scan and populate all volumes
   int disksDetected = DISK_Initialize();
   if (disksDetected < 0)
   {
      logfmt(LOG_ERROR, "[FS] Disk initialization failed\n");
      return false;
   }
   return true;
}
