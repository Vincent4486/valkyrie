// SPDX-License-Identifier: GPL-3.0-only

#include "disk.h"
#include <drivers/ata/ata.h>
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
