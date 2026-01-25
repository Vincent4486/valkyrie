// SPDX-License-Identifier: GPL-3.0-only

#include "disk.h"
#include <drivers/ata/ata.h>
#include <drivers/fdc/fdc.h>
#include <fs/devfs/devfs.h>
#include <fs/fat/fat.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>

// Updated: Scan all disks and populate volumes
int DISK_Initialize()
{
   DISK_Scan();

   return 0;
}

int DISK_Scan()
{
   for (int i = 0; i < MAX_DISKS; i++)
   {
      g_SysInfo->volume[i].disk = NULL;
   }

   DISK detectedDisks[32]; // Temp array for detected disks
   int totalDisks = 0;

   // Scan floppies
   totalDisks += FDC_Scan(detectedDisks + totalDisks, 32 - totalDisks);

   // Scan ATA
   totalDisks += ATA_Scan(detectedDisks + totalDisks, 32 - totalDisks);

   // Populate volume[] with detected disks and partitions
   for (int i = 0; i < totalDisks; i++)
   {
      DISK *source = &detectedDisks[i];
      // Keep disk metadata on the heap so the pointer stays valid beyond this
      // stack frame.
      DISK *disk = (DISK *)kmalloc(sizeof(DISK));
      if (!disk)
      {
         printf("[DISK] Failed to allocate disk entry for %s\n", source->brand);
         continue;
      }
      memcpy(disk, source, sizeof(DISK));
      int volumeIndex = -1;
      for (int j = 0; j < 32; j++)
      {
         if (g_SysInfo->volume[j].disk == NULL)
         {
            volumeIndex = j;
            break;
         }
      }
      if (volumeIndex == -1) break; // No slots

      int part_count = 0;
      Partition **parts = MBR_DetectPartition(disk, &part_count);

      for (int p = 0; p < part_count; p++)
      {
         // Find next free slot for each partition
         while (volumeIndex < 32 && g_SysInfo->volume[volumeIndex].disk != NULL)
         {
            volumeIndex++;
         }
         if (volumeIndex >= 32) break;

         // Copy partition data into system volume table
         g_SysInfo->volume[volumeIndex] = *(parts[p]);
         logfmt(
             LOG_INFO,
             "[DISK] Populated volume[%d]: Offset=%u, Size=%u, Type=0x%02x\n",
             volumeIndex, g_SysInfo->volume[volumeIndex].partitionOffset,
             g_SysInfo->volume[volumeIndex].partitionSize,
             g_SysInfo->volume[volumeIndex].partitionType);

         // Initialize filesystem on this partition (only for FAT types)
         Partition *volume = &g_SysInfo->volume[volumeIndex];
         // Defensive: ensure partition has a backing disk before initializing
         if (!volume->disk)
         {
            printf("[DISK] Skipping init: volume[%d] has no disk pointer\n",
                   volumeIndex);
            volumeIndex++;
            continue;
         }
         uint8_t partType = volume->partitionType & 0xFF;
         if (partType == 0x04 || partType == 0x06 || partType == 0x0B ||
             partType == 0x0C)
         {
            if (FAT_Initialize(volume))
            {
               // Allocate and populate Filesystem struct
               Filesystem *fs = (Filesystem *)kmalloc(sizeof(Filesystem));
               if (fs)
               {
                  fs->mounted = 0; // Not mounted yet, just initialized
                  fs->read_only = 0;
                  fs->block_size = 512;
                  fs->type = FAT32; // TODO: detect actual FAT type
                  fs->ops = NULL;   // Will be set during FS_Mount
                  volume->fs = fs;
               }
               else
               {
                  // FAT initialized but we couldn't allocate filesystem struct
                  // Ensure we don't leave a dangling pointer
                  printf("[DISK] Warning: FAT init succeeded but allocation "
                         "failed for volume[%d]\n",
                         volumeIndex);
                  volume->fs = NULL;
               }
            }
            else
            {
               logfmt(LOG_ERROR,
                      "[DISK] Failed to initialize FAT on volume[%d]\n",
                      volumeIndex);
               volume->fs = NULL; // Explicitly clear to avoid later deref
            }
         }
         else
         {
            logfmt(
                LOG_INFO,
                "[DISK] Skipping filesystem init for partition type 0x%02x\n",
                partType);
         }

         volumeIndex++;
      }

      // Free allocated partition structures
      if (parts)
      {
         for (int p = 0; p < part_count; p++)
         {
            if (parts[p]) free(parts[p]);
         }
         free(parts);
      }
   }
   g_SysInfo->disk_count = totalDisks;

   return 0;
}

int DISK_GetDevfsIndex()
{
   // Find the devfs volume (disk == NULL && fs != NULL && fs->ops == devfs_ops)
   for (int i = 0; i < MAX_DISKS; i++)
   {
      if (g_SysInfo->volume[i].disk == NULL && g_SysInfo->volume[i].fs != NULL &&
          g_SysInfo->volume[i].fs->ops == DEVFS_GetVFSOperations())
      {
         return i;
      }
   }
   return -1;
}

void DISK_LBA2CHS(DISK *disk, uint32_t lba, uint16_t *cylinderOut,
                  uint16_t *sectorOut, uint16_t *headOut)
{
   // sector = (LBA % sectors per track + 1)
   *sectorOut = lba % disk->sectors + 1;

   // cylinder = (LBA / sectors per track) / heads
   *cylinderOut = (lba / disk->sectors) / disk->heads;

   // head = (LBA / sectors per track) % heads
   *headOut = (lba / disk->sectors) % disk->heads;
}

bool DISK_ReadSectors(DISK *disk, uint32_t lba, uint8_t sectors, void *dataOut)
{
   if (!disk || sectors == 0 || !dataOut) return false;

   if (disk->type == DISK_TYPE_FLOPPY)
   {
      /* Floppy drive: use the kernel FDC driver which speaks directly to the
       * floppy controller. This avoids relying on BIOS INT13 services from
       * the kernel.
       */
      int rc = FDC_ReadLba(disk, lba, (uint8_t *)dataOut, sectors);
      if (rc != 0) return false;
      return true;
   }
   else if (disk->type == DISK_TYPE_ATA)
   {
      /* Hard disk (ATA): use the kernel ATA driver with primary master
       * channel/drive.
       */
      int rc = ATA_Read(disk, lba, (uint8_t *)dataOut, sectors);
      if (rc != 0) return false;
      return true;
   }

   return false;
}

bool DISK_WriteSectors(DISK *disk, uint32_t lba, uint8_t sectors,
                       const void *dataIn)
{
   if (!disk || sectors == 0 || !dataIn) return false;

   if (disk->type == DISK_TYPE_FLOPPY)
   {
      /* Floppy drive: use the kernel FDC driver which speaks directly to the
       * floppy controller.
       */
      int rc = FDC_WriteLba(disk, lba, (const uint8_t *)dataIn, sectors);
      if (rc != 0) return false;
      return true;
   }
   else if (disk->type == DISK_TYPE_ATA)
   {
      /* Hard disk (ATA): use the kernel ATA driver with primary master
       * channel/drive.
       */
      int rc = ATA_Write(disk, lba, (const uint8_t *)dataIn, sectors);
      if (rc != 0) return false;
      return true;
   }

   return false;
}
