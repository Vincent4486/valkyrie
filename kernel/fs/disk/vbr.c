// SPDX-License-Identifier: GPL-3.0-only

#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <valecium/fs.h>

#include "disk.h"

/*
 * VBR_ProbeIdentity
 *
 * Reads the Volume Boot Record (VBR) — the first sector of `vol` — and
 * extracts the FAT volume serial number (UUID) and volume label.
 *
 * The absolute byte address of the boot record on the physical medium is:
 *
 *   AbsoluteOffset = (Partition.partitionOffset × 512) + BootRecordOffset
 *
 * where BootRecordOffset = 0 for the partition-relative VBR, giving us
 * LBA = partitionOffset + 0 = partitionOffset.
 *
 * After filling vol->uuid and vol->label, the function compares the probed
 * metadata against the `root=` kernel command-line argument.  If the value
 * matches (formats LABEL=<name> or PARTUUID=<hex>), vol->isRootPartition
 * is set to true.
 *
 * Only FAT12/16/32 partition types are probed; other types are skipped.
 */
void VBR_ProbeIdentity(Partition *vol, const char *rootCmdVal)
{
   if (!vol || !vol->disk) return;

   uint8_t ptype = (uint8_t)(vol->partitionType & 0xFFu);

   /* Only probe recognised FAT types */
   bool isFAT32 = (ptype == 0x0Bu || ptype == 0x0Cu);
   bool isFAT1X = (ptype == 0x01u || ptype == 0x04u || ptype == 0x06u);
   if (!isFAT32 && !isFAT1X) return;

   /* Allocate a single-sector (512-byte) scratch buffer */
   uint8_t *vbr = (uint8_t *)kmalloc(512);
   if (!vbr) return;

   /*
    * Read the VBR — absolute LBA = partitionOffset, relative offset = 0.
    * AbsoluteOffset = (partitionOffset × 512) + 0
    */
   if (!DISK_ReadSectors(vol->disk, vol->partitionOffset, 1, vbr))
   {
      free(vbr);
      return;
   }

   /* Verify the standard 0x55/0xAA boot sector signature */
   if (vbr[VBR_SIG_OFFSET] != VBR_SIG_BYTE0 ||
       vbr[VBR_SIG_OFFSET + 1] != VBR_SIG_BYTE1)
   {
      free(vbr);
      return;
   }

   /* Select the correct BPB offsets based on the FAT sub-type */
   uint32_t off_id = isFAT32 ? FAT32_BPB_VOLID : FAT1X_BPB_VOLID;
   uint32_t off_lab = isFAT32 ? FAT32_BPB_VOLLIB : FAT1X_BPB_VOLLIB;

   /* Extract 4-byte volume serial number */
   memcpy(&vol->uuid, vbr + off_id, 4);

   /* Extract 11-byte volume label and null-terminate */
   memcpy(vol->label, vbr + off_lab, 11);
   vol->label[11] = '\0';

   /* Trim trailing space padding (0x20) that FAT pads short labels with */
   for (int i = 10; i >= 0 && vol->label[i] == ' '; i--) vol->label[i] = '\0';

   logfmt(LOG_INFO, "[DISK] VBR identity: uuid=0x%08X label=\"%s\"\n",
          vol->uuid, vol->label);

   /* -----------------------------------------------------------------------
    * Root partition identification — compare against root= cmdline value.
    * Supported formats:
    *   LABEL=<volume-label>          (case-sensitive 11-char match)
    *   PARTUUID=<8 uppercase hex>    (e.g. PARTUUID=DEADBEEF)
    * -------------------------------------------------------------------- */
   if (!rootCmdVal)
   {
      free(vbr);
      return;
   }

   if (strncmp(rootCmdVal, "LABEL=", 6) == 0)
   {
      if (strcmp(vol->label, rootCmdVal + 6) == 0)
      {
         vol->isRootPartition = true;
         logfmt(LOG_INFO, "[DISK] Root partition tagged by LABEL=\"%s\"\n",
                vol->label);
      }
   }
   else if (strncmp(rootCmdVal, "PARTUUID=", 9) == 0)
   {
      /* Format uuid as 8 uppercase hex digits and compare */
      char uuidStr[9];
      snprintf(uuidStr, sizeof(uuidStr), "%08X", vol->uuid);
      if (strcmp(uuidStr, rootCmdVal + 9) == 0)
      {
         vol->isRootPartition = true;
         logfmt(LOG_INFO, "[DISK] Root partition tagged by PARTUUID=%s\n",
                uuidStr);
      }
   }

   free(vbr);
}