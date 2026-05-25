// SPDX-License-Identifier: GPL-3.0-only
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MBR partition entry (16 bytes) */
typedef struct
{
   uint8_t boot_flag;     /* 0x80 = active/bootable               */
   uint8_t start_chs[3];  /* CHS address of first sector          */
   uint8_t type;          /* partition type identifier            */
   uint8_t end_chs[3];    /* CHS address of last sector           */
   uint32_t lba_start;    /* LBA of first sector (little-endian)  */
   uint32_t sector_count; /* number of sectors in partition       */
} __attribute__((packed)) MBR_PartitionEntry;

#define MBR_PARTITION_COUNT 4
#define MBR_SIGNATURE 0xAA55

extern int DISK_Read(uint8_t drive, uint16_t cylinder, uint8_t sector,
                     uint8_t head, uint8_t count, void *buffer);

bool MBR_Probe(int driveId)
{
   uint8_t sector[512];

   /* Read the first sector (CHS 0:0:1) — the MBR */
   if (DISK_Read((uint8_t)driveId, 0, 1, 0, 1, sector) != 0) return false;

   /* Check for the 0xAA55 boot signature at offset 510 */
   uint16_t sig = (uint16_t)(sector[510] | ((uint16_t)sector[511] << 8));
   return (sig == MBR_SIGNATURE);
}

int MBR_List(int driveId, int **offset)
{
   uint8_t sector[512];
   static int offsets[MBR_PARTITION_COUNT];
   int count = 0;

   /* Read the MBR */
   if (DISK_Read((uint8_t)driveId, 0, 1, 0, 1, sector) != 0) return -1;

   /* Verify signature */
   uint16_t sig = (uint16_t)(sector[510] | ((uint16_t)sector[511] << 8));
   if (sig != MBR_SIGNATURE) return -1;

   /* Parse the four primary partition entries starting at offset 446 */
   for (int i = 0; i < MBR_PARTITION_COUNT; i++)
   {
      MBR_PartitionEntry *entry = (MBR_PartitionEntry *)&sector[446 + i * 16];

      /* A partition entry with type 0 is unused */
      if (entry->type != 0)
      {
         offsets[count++] = (int)entry->lba_start;
      }
   }

   *offset = offsets;
   return count;
}