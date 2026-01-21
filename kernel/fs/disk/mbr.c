// SPDX-License-Identifier: AGPL-3.0-or-later

#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stddef.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>

typedef struct
{
   // 0x00	1	Drive attributes (bit 7 set = active or bootable)
   uint8_t attributes;

   // 0x01	3	CHS Address of partition start
   uint8_t chsStart[3];

   // 0x04	1	Partition type
   uint8_t partitionType;

   // 0x05	3	CHS address of last partition sector
   uint8_t chsEnd[3];

   // 0x08	4	LBA of partition start
   uint32_t lbaStart;

   // 0x0C	4	Number of sectors in partition
   uint32_t size;

} __attribute__((packed)) MBR_Entry;

Partition **MBR_DetectPartition(DISK *disk, int *outCount)
{
   if (!outCount || !disk) return NULL;

   *outCount = 0;

   // Floppy: treat entire device as a single partition
   if (disk->type == DISK_TYPE_FLOPPY)
   {
      Partition **list = (Partition **)kmalloc(sizeof(Partition *));
      if (!list) return NULL;

      Partition *part = (Partition *)kzalloc(sizeof(Partition));
      if (!part) return NULL;

      part->disk = disk;
      part->partitionOffset = 0;
      part->partitionSize = (uint32_t)(disk->cylinders) *
                            (uint32_t)(disk->heads) * (uint32_t)(disk->sectors);

      list[0] = part;
      *outCount = 1;
      return list;
   }

   // Hard disk: inspect MBR
   Partition **list = (Partition **)kzalloc(sizeof(Partition *) * 4);
   uint8_t *mbr_buffer = (uint8_t *)kmalloc(512);
   if (!mbr_buffer)
   {
      list[0] = NULL;
      *outCount = 0;
      return list;
   }
   bool ok = DISK_ReadSectors(disk, 0, 1, mbr_buffer);

   int count = 0;

   if (ok)
   {
      void *partition_entry = &mbr_buffer[446];

      for (int p = 0; p < 4; p++)
      {
         uint8_t *entry = (uint8_t *)partition_entry + (p * 16);
         uint8_t type = entry[4];

         // FAT variants we support
         if (type == 0x04 || type == 0x06 || type == 0x0B || type == 0x0C)
         {
            Partition *part = (Partition *)kzalloc(sizeof(Partition));
            if (!part) continue;

            part->disk = disk;
            part->partitionOffset = *(uint32_t *)(entry + 8);
            part->partitionSize = *(uint32_t *)(entry + 12);
            part->partitionType = type;

            list[count++] = part;
         }
      }
   }

   // If nothing detected, fabricate a default partition so higher layers can
   // proceed
   if (count == 0)
   {
      Partition *part = (Partition *)kzalloc(sizeof(Partition));
      if (part)
      {
         part->disk = disk;
         part->partitionOffset = ok ? 16 : 0;
         part->partitionSize = 0x100000;
         list[0] = part;
         count = 1;
      }
   }

   free(mbr_buffer);
   *outCount = count;
   return list;
}