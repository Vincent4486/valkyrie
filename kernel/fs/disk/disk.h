// SPDX-License-Identifier: GPL-3.0-only

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
*/

#ifndef DISK_H
#define DISK_H

#include <stdbool.h>
#include <stdint.h>

// Disk type constants
#define DISK_TYPE_FLOPPY 0
#define DISK_TYPE_ATA 1

typedef struct DISK_Operations
{

} DISK_Operations;

typedef struct
{
   uint8_t id;   // bios drive number
   uint8_t type; // DISK_TYPE_FLOPPY or DISK_TYPE_ATA
   uint16_t cylinders;
   uint16_t sectors;
   uint16_t heads;

   void *private;
   char brand[41]; // Model name (up to 40 chars + null)
   uint64_t size;  // Total size in bytes
} DISK;

int DISK_Initialize();
int DISK_Scan();
bool DISK_ReadSectors(DISK *disk, uint32_t lba, uint8_t sectors,
                      void *lowerDataOut);
bool DISK_WriteSectors(DISK *disk, uint32_t lba, uint8_t sectors,
                       const void *dataIn);

/* Forward declaration to avoid circular include with fs.h */
struct Filesystem;
typedef struct Filesystem Filesystem;

typedef struct Partition
{
   DISK *disk;
   uint32_t partitionOffset;
   uint32_t partitionSize;
   uint32_t partitionType;

   Filesystem *fs;

   uint32_t uuid;
   char label[12];
   bool isRootPartition;
} Partition;

Partition **MBR_DetectPartition(DISK *disk, int *outCount);

bool Partition_ReadSectors(Partition *disk, uint32_t lba, uint8_t sectors,
                           void *lowDataOut);

bool Partition_WriteSectors(Partition *part, uint32_t lba, uint8_t sectors,
                            const void *lowerDataIn);

#endif