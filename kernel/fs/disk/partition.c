// SPDX-License-Identifier: GPL-3.0-only

#include "disk.h"
#include <drivers/ata/ata.h>
#include <fs/devfs/devfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <sys/sys.h>

bool Partition_ReadSectors(Partition *part, uint32_t lba, uint8_t sectors,
                           void *lowerDataOut)
{
   if (!part) return false;

   /* Defensive: avoid dereferencing possibly-dangling Partition pointers.
    * Accept Partition pointers that live in the global volume table or in
    * the kernel heap; otherwise bail out to prevent a kernel page-fault.
    */
   uintptr_t p = (uintptr_t)part;
   uintptr_t heap_start = mem_heap_start();
   uintptr_t heap_end = mem_heap_end();
   uintptr_t volumes_start = (uintptr_t)&g_SysInfo->volume[0];
   uintptr_t volumes_end = (uintptr_t)(&g_SysInfo->volume[MAX_DISKS]);

   if (!((p >= volumes_start && p < volumes_end) ||
         (heap_start != 0 && p >= heap_start && p < heap_end)))
   {
      printf("[PART] Invalid partition pointer: 0x%08x\n", (unsigned int)p);
      return false;
   }

   if (!part->disk) return false;

   return DISK_ReadSectors(part->disk, lba + part->partitionOffset, sectors,
                           lowerDataOut);
}

bool Partition_WriteSectors(Partition *part, uint32_t lba, uint8_t sectors,
                            const void *lowerDataIn)
{
   if (!part) return false;

   uintptr_t p = (uintptr_t)part;
   uintptr_t heap_start = mem_heap_start();
   uintptr_t heap_end = mem_heap_end();
   uintptr_t volumes_start = (uintptr_t)&g_SysInfo->volume[0];
   uintptr_t volumes_end = (uintptr_t)(&g_SysInfo->volume[MAX_DISKS]);

   if (!((p >= volumes_start && p < volumes_end) ||
         (heap_start != 0 && p >= heap_start && p < heap_end)))
   {
      printf("[PART] Invalid partition pointer: 0x%08x\n", (unsigned int)p);
      return false;
   }

   if (!part->disk) return false;

   return DISK_WriteSectors(part->disk, lba + part->partitionOffset, sectors,
                            lowerDataIn);
}

/*
 * Devfs operations for partition devices
 */

uint32_t Partition_DevfsRead(DEVFS_DeviceNode *node, uint32_t offset,
                             uint32_t size, void *buffer)
{
   if (!node || !node->private_data || !buffer) return 0;
   
   Partition *part = (Partition *)node->private_data;
   if (!part->disk) return 0;
   
   /* Calculate sector-based read within partition */
   uint32_t sector_size = 512;
   uint32_t start_sector = offset / sector_size;
   uint32_t sectors_needed = (size + sector_size - 1) / sector_size;
   
   /* Bounds check */
   if (start_sector >= part->partitionSize) return 0;
   if (start_sector + sectors_needed > part->partitionSize) {
      sectors_needed = part->partitionSize - start_sector;
   }
   
   /* Allocate temporary buffer */
   uint8_t *temp = kmalloc(sectors_needed * sector_size);
   if (!temp) return 0;
   
   /* Read from partition (uses partition-relative LBA) */
   if (!Partition_ReadSectors(part, start_sector, sectors_needed, temp)) {
      free(temp);
      return 0;
   }
   
   /* Copy requested portion */
   uint32_t offset_in_sector = offset % sector_size;
   uint32_t bytes_to_copy = size;
   if (bytes_to_copy > sectors_needed * sector_size - offset_in_sector) {
      bytes_to_copy = sectors_needed * sector_size - offset_in_sector;
   }
   
   memcpy(buffer, temp + offset_in_sector, bytes_to_copy);
   free(temp);
   
   return bytes_to_copy;
}

uint32_t Partition_DevfsWrite(DEVFS_DeviceNode *node, uint32_t offset,
                              uint32_t size, const void *buffer)
{
   if (!node || !node->private_data || !buffer) return 0;
   
   Partition *part = (Partition *)node->private_data;
   if (!part->disk) return 0;
   
   /* Calculate sector-based write */
   uint32_t sector_size = 512;
   uint32_t start_sector = offset / sector_size;
   uint32_t sectors_needed = (size + sector_size - 1) / sector_size;
   uint32_t offset_in_sector = offset % sector_size;
   
   /* Bounds check */
   if (start_sector >= part->partitionSize) return 0;
   if (start_sector + sectors_needed > part->partitionSize) {
      sectors_needed = part->partitionSize - start_sector;
   }
   
   /* Allocate temp buffer */
   uint8_t *temp = kmalloc(sectors_needed * sector_size);
   if (!temp) return 0;
   
   /* Read-modify-write for partial sectors */
   if (offset_in_sector != 0 || (size % sector_size) != 0) {
      if (!Partition_ReadSectors(part, start_sector, sectors_needed, temp)) {
         free(temp);
         return 0;
      }
   }
   
   /* Copy new data */
   uint32_t bytes_to_write = size;
   if (bytes_to_write > sectors_needed * sector_size - offset_in_sector) {
      bytes_to_write = sectors_needed * sector_size - offset_in_sector;
   }
   memcpy(temp + offset_in_sector, buffer, bytes_to_write);
   
   /* Write back */
   if (!Partition_WriteSectors(part, start_sector, sectors_needed, temp)) {
      free(temp);
      return 0;
   }
   
   free(temp);
   return bytes_to_write;
}
