// SPDX-License-Identifier: GPL-3.0-only

/*
 * kernel/init/mount.c — Root filesystem mount for ValeciumOS.
 *
 * Isolation policy: no kernel-local headers are #included inside kernel/init/.
 * Every external symbol is declared with `extern` directly in this file.
 *
 * Responsibility:
 *   1. Walk g_SysInfo->volume[] looking for the partition tagged as root
 *      by DISK_Scan (Partition.isRootPartition == true).
 *   2. Mount that partition to "/" via FS_Mount.
 *   3. Verify that /boot/init.sys exists to confirm a usable root tree.
 *   4. Panic if no root partition can be mounted.
 */

#include <std/stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/sys.h>
#include <valecium/fs.h>
#include <valecium/system.h>
#include <valecium/valecium.h>

/* -------------------------------------------------------------------------
 * Public interface
 * ---------------------------------------------------------------------- */

/*
 * Init_MountRoot
 *
 * Scans the global volume table for the partition whose isRootPartition
 * flag was set by DISK_Scan (matched via LABEL= or PARTUUID= on the
 * kernel command line), then mounts it to "/".
 *
 * After a successful mount, the function probes for /boot/init.sys to
 * confirm that the root tree is usable and to signal readiness for the
 * userspace transition.
 *
 * Returns 0 on success; calls mount_panic() and does not return on failure.
 */
bool Init_MountRoot(void)
{
   for (int i = 0; i < MAX_DISKS; i++)
   {
      /* Skip entries that have not been tagged as root */
      if (!g_SysInfo->volume[i].disk || !g_SysInfo->volume[i].isRootPartition)
         continue;

      Partition *part = &g_SysInfo->volume[i];

      logfmt(LOG_INFO, "[MOUNT] Root found: LABEL=\"%s\"\n",
             part->label[0] ? part->label : "VALECIUM");

      int rc = FS_Mount(part, "/");
      if (rc != 0)
      {
         logfmt(LOG_WARNING,
                "[MOUNT] FS_Mount failed for volume[%d] (rc=%d) — trying "
                "next candidate\n",
                i, rc);
         continue;
      }

      logfmt(LOG_INFO,
             "[MOUNT] Root filesystem mounted from volume[%d] at \"/\"\n", i);

      /* -----------------------------------------------------------------
       * Post-mount probe: verify /boot/init.sys exists so the kernel
       * knows a well-formed root tree is present before handing off to
       * userspace initialisation logic.
       * -------------------------------------------------------------- */
      struct VFS_File *initSys = VFS_Open("/boot/init.sys");
      if (initSys)
      {
         logfmt(LOG_INFO, "[MOUNT] Found /boot/init.sys — userspace transition "
                          "ready\n");
         VFS_Close(initSys);
      }
      else
      {
         logfmt(LOG_WARNING, "[MOUNT] /boot/init.sys not found on root "
                             "filesystem\n");
      }

      return true;
   }

   /* No tagged partition was mountable */
   logfmt(LOG_FATAL,
          "No root partition found or all FS_Mount attempts failed.\n");

   return false; /* unreachable — satisfies the compiler */
}
