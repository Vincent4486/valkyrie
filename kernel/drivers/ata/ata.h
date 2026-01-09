// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef ATA_H
#define ATA_H
#include <fs/fs.h>
#include <stddef.h>
#include <stdint.h>
#define ATA_SECTOR_SIZE 512

// IDE Channel constants
#define ATA_CHANNEL_PRIMARY 0
#define ATA_CHANNEL_SECONDARY 1

// Drive constants
#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE 1

/**
 * Initialize ATA driver for a specific drive
 * @param channel - IDE channel (ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY)
 * @param drive - Drive on channel (ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE)
 * @param partition_start - Absolute LBA where partition starts
 * @param partition_size - Total sectors in partition
 * @return 0 on success, -1 on failure
 */
int ATA_Init(int channel, int drive, uint32_t partition_start,
             uint32_t partition_size);

/**
 * Read sectors from ATA drive
 * @param channel - IDE channel (ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY)
 * @param drive - Drive on channel (ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE)
 * @param lba - Logical block address (relative to partition start)
 * @param buffer - Destination buffer (must be at least count*512 bytes)
 * @param count - Number of sectors to read
 * @return 0 on success, -1 on failure
 */
int ATA_Read(int channel, int drive, uint32_t lba, uint8_t *buffer,
             uint32_t count);

/**
 * Write sectors to ATA drive
 * @param channel - IDE channel (ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY)
 * @param drive - Drive on channel (ATA_DRIVE_MASTER or ATA_DRIVE_SLAVE)
 * @param lba - Logical block address (relative to partition start)
 * @param buffer - Source buffer
 * @param count - Number of sectors to write
 * @return 0 on success, -1 on failure
 */
int ATA_Write(int channel, int drive, uint32_t lba, const uint8_t *buffer,
              uint32_t count);

/**
 * Perform software reset on ATA channel
 * @param channel - IDE channel (ATA_CHANNEL_PRIMARY or ATA_CHANNEL_SECONDARY)
 */
void ATA_Reset(int channel);

/**
 * Identify ATA drive
 * @param channel - IDE channel
 * @param drive - Drive on channel
 * @param buffer - 256-word buffer to store IDENTIFY data
 * @return 0 on success, -1 on failure
 */
int ATA_Identify(int channel, int drive, uint16_t *buffer);

/**
 * Scan for ATA disks
 * @param disks - Array to store detected disks
 * @param maxDisks - Maximum number of disks to detect
 * @return Number of disks detected
 */
int ATA_Scan(DISK *disks, int maxDisks);

#endif