// SPDX-License-Identifier: AGPL-3.0-or-later

#include <fs/devfs/devfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stddef.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>

static DEVFS_DeviceOps partition_ops = {
   .read = Partition_DevfsRead,
   .write = Partition_DevfsWrite
};

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

      /* Register floppy partition in devfs: fd0p1, fd1p1, etc. */
      char devname[8];
      devname[0] = 'f';
      devname[1] = 'd';
      devname[2] = '0' + (disk->id & 0x0F);
      devname[3] = 'p';
      devname[4] = '1';
      devname[5] = '\0';
      uint32_t part_size = part->partitionSize * 512;
      DEVFS_RegisterDevice(devname, DEVFS_TYPE_BLOCK, 2, 
                           (disk->id & 0x0F) * 16 + 1, part_size, &partition_ops, part);

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

            /* Register ATA partition in devfs: hda1, hda2, hdb1, etc. */
            char devname[8];
            /* disk->id is BIOS drive number (0x80, 0x81, ...) */
            int disk_idx = (disk->id >= 0x80) ? (disk->id - 0x80) : 0;
            devname[0] = 'h';
            devname[1] = 'd';
            devname[2] = 'a' + disk_idx;
            devname[3] = '1' + count; /* partition number */
            devname[4] = '\0';
            uint32_t part_size = part->partitionSize * 512;
            DEVFS_RegisterDevice(devname, DEVFS_TYPE_BLOCK, 3,
                                 disk_idx * 16 + count + 1, part_size, &partition_ops, part);

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