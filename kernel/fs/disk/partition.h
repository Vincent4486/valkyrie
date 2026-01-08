// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PARTITION_H
#define PARTITION_H

#include "disk.h"
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration to avoid circular include with fs.h */
struct Filesystem;
typedef struct Filesystem Filesystem;

typedef struct
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