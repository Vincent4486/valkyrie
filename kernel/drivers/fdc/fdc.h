// SPDX-License-Identifier: GPL-3.0-only

#ifndef FDC_H
#define FDC_H
#include <fs/fs.h> // For DISK struct
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FDC_SECTOR_SIZE 512

// Read 'count' sectors from 'lba' into 'buffer' (buffer must be at least
// count*512 bytes) Returns 0 on success, nonzero on error
int FDC_ReadLba(uint8_t drive, uint32_t lba, uint8_t *buffer, size_t count);

// Write 'count' sectors from 'buffer' to 'lba'
// Returns 0 on success, nonzero on error
int FDC_WriteLba(uint8_t drive, uint32_t lba, const uint8_t *buffer,
                 size_t count);

// Seek to specified head and track
// Returns true on success, false on error
bool FDC_Seek(uint8_t head, uint8_t track);

void FDC_Reset(void);

/**
 * Scan for floppy disks
 * @param disks - Array to store detected disks
 * @param maxDisks - Maximum number of disks to detect
 * @return Number of disks detected
 */
int FDC_Scan(DISK *disks, int maxDisks);

#endif