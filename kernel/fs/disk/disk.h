// SPDX-License-Identifier: GPL-3.0-only

#ifndef DISK_H
#define DISK_H

#include <stdbool.h>
#include <stdint.h>

// Disk type constants
#define DISK_TYPE_FLOPPY 0
#define DISK_TYPE_ATA 1

typedef struct
{
   uint8_t id;   // bios drive number
   uint8_t type; // DISK_TYPE_FLOPPY or DISK_TYPE_ATA
   uint16_t cylinders;
   uint16_t sectors;
   uint16_t heads;
   char brand[41]; // Model name (up to 40 chars + null)
   uint64_t size;  // Total size in bytes
} DISK;

int DISK_Initialize();
int DISK_Scan();
bool DISK_ReadSectors(DISK *disk, uint32_t lba, uint8_t sectors,
                      void *lowerDataOut);
bool DISK_WriteSectors(DISK *disk, uint32_t lba, uint8_t sectors,
                       const void *dataIn);

#endif