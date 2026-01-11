// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ata.h"
#include <hal/io.h>
#include <std/stdio.h>
#include <stdint.h>
#include <sys/sys.h>
#include <valkyrie/system.h>

// ATA register offsets from base port
#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_FEATURES 0x01
#define ATA_REG_NSECTOR 0x02
#define ATA_REG_LBA_LOW 0x03
#define ATA_REG_LBA_MID 0x04
#define ATA_REG_LBA_HIGH 0x05
#define ATA_REG_DEVICE 0x06
#define ATA_REG_STATUS 0x07
#define ATA_REG_COMMAND 0x07

// ATA status bits
#define ATA_STATUS_BSY 0x80  // Busy
#define ATA_STATUS_DRDY 0x40 // Device ready
#define ATA_STATUS_DRQ 0x08  // Data request
#define ATA_STATUS_ERR 0x01  // Error

// ATA commands
#define ATA_CMD_READ_PIO 0x20  // 28-bit LBA read
#define ATA_CMD_WRITE_PIO 0x30 // 28-bit LBA write
#define ATA_CMD_IDENTIFY 0xEC  // Identify device

// Driver data structure
typedef struct
{
   uint32_t partition_length; // Total sectors
   uint32_t start_lba;        // Partition start (should be 0 for absolute LBA)
   uint16_t dcr_port;         // Alt status/DCR port
   uint16_t tf_port;          // Task file base port
   uint8_t slave_bits;        // Master/slave bits (0xA0 or 0xB0)
} ata_driver_t;

// Global driver instances
static ata_driver_t primary_master = {.partition_length = 0x100000,
                                      .start_lba = 0,
                                      .dcr_port = 0x3F6,
                                      .tf_port = 0x1F0,
                                      .slave_bits = 0xA0};

static ata_driver_t primary_slave = {.partition_length = 0x100000,
                                     .start_lba = 0,
                                     .dcr_port = 0x3F6,
                                     .tf_port = 0x1F0,
                                     .slave_bits = 0xB0};

static ata_driver_t secondary_master = {.partition_length = 0x100000,
                                        .start_lba = 0,
                                        .dcr_port = 0x376,
                                        .tf_port = 0x170,
                                        .slave_bits = 0xA0};

static ata_driver_t secondary_slave = {.partition_length = 0x100000,
                                       .start_lba = 0,
                                       .dcr_port = 0x376,
                                       .tf_port = 0x170,
                                       .slave_bits = 0xB0};

/**
 * Get driver for channel and drive
 */
static ata_driver_t *ata_get_driver(int channel, int drive)
{
   if (channel == 0 && drive == 0) return &primary_master;
   if (channel == 0 && drive == 1) return &primary_slave;
   if (channel == 1 && drive == 0) return &secondary_master;
   if (channel == 1 && drive == 1) return &secondary_slave;
   return NULL;
}

/**
 * Wait for drive to be ready (not busy)
 */
static int ata_wait_busy(uint16_t tf_port)
{
   // Timeout: ~1 second at 1MHz CPU (~1 million iterations per ms)
   // Each iteration reads status register, so adjust based on CPU speed
   int timeout = 10000; // Much shorter timeout

   while (timeout--)
   {
      uint8_t status = HAL_inb(tf_port + ATA_REG_STATUS);
      if (!(status & ATA_STATUS_BSY)) return 0;

      // Small delay to prevent bus saturation
      for (volatile int i = 0; i < 100; i++);
   }

   return -1; // Timeout
}

/**
 * Wait for data ready
 */
static int ata_wait_drq(uint16_t tf_port)
{
   // Timeout: similar to wait_busy
   int timeout = 10000;

   while (timeout--)
   {
      uint8_t status = HAL_inb(tf_port + ATA_REG_STATUS);
      if (status & ATA_STATUS_DRQ) return 0;
      if (status & ATA_STATUS_ERR)
      {
         return -1;
      }

      // Small delay
      for (volatile int i = 0; i < 100; i++);
   }

   return -1; // Timeout
}

/**
 * Wait for drive to be ready (not busy and DRDY set)
 */
static int ata_wait_for_ready(uint16_t tf_port)
{
   int timeout = 10000;

   while (timeout--)
   {
      uint8_t status = HAL_inb(tf_port + ATA_REG_STATUS);
      if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRDY)) return 0;

      // Small delay
      for (volatile int i = 0; i < 100; i++);
   }

   return -1; // Timeout
}

/**
 * Wait for data ready (DRQ set)
 */
static int ata_wait_for_data(uint16_t tf_port)
{
   int timeout = 10000;

   while (timeout--)
   {
      uint8_t status = HAL_inb(tf_port + ATA_REG_STATUS);
      if (status & ATA_STATUS_DRQ) return 0;
      if (status & ATA_STATUS_ERR) return -1;

      // Small delay
      for (volatile int i = 0; i < 100; i++);
   }

   return -1; // Timeout
}

/**
 * Perform software reset on ATA channel
 */
static void ata_soft_reset(uint16_t dcr_port)
{
   // Set SRST bit (software reset)
   HAL_outb(dcr_port, 0x04);

   // Wait a bit
   for (volatile int i = 0; i < 100000; i++);

   // Clear SRST bit
   HAL_outb(dcr_port, 0x00);

   // Wait for reset to complete
   for (volatile int i = 0; i < 100000; i++);
}

/**
 * Initialize ATA driver for a specific drive
 */
int ATA_Init(int channel, int drive, uint32_t partition_start,
             uint32_t partition_size)
{
   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv) return -1;

   drv->start_lba = 0; // We use absolute LBA
   drv->partition_length = partition_size;

   // Perform software reset
   ata_soft_reset(drv->dcr_port);

   return 0;
}

/**
 * Read sectors from ATA drive using PIO mode (28-bit LBA)
 */
int ATA_Read(int channel, int drive, uint32_t lba, uint8_t *buffer,
             uint32_t count)
{
   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv || !buffer || count == 0) return -1;

   // Limit to 255 sectors per read (8-bit sector count)
   if (count > 255)
   {
      count = 255;
   }

   // Wait for drive to be ready
   if (ata_wait_busy(drv->tf_port) != 0)
   {
      return -1;
   }

   // Prepare device register value with master/slave bits, LBA flag, and upper
   // LBA bits (bits 24-27) Note: must set the LBA bit (0x40) when using LBA
   // addressing, otherwise the device may ABRT.
   uint8_t device = drv->slave_bits | 0x40 | ((lba >> 24) & 0x0F);

   // Write all command registers in the correct sequence
   // This is critical - must follow ATA protocol
   HAL_outb(drv->tf_port + ATA_REG_NSECTOR, count & 0xFF);
   HAL_outb(drv->tf_port + ATA_REG_LBA_LOW, (lba & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_LBA_MID, ((lba >> 8) & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_LBA_HIGH, ((lba >> 16) & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_DEVICE, device);

   // Small delay to allow registers to settle
   for (volatile int i = 0; i < 50000; i++);

   // Issue READ SECTORS command
   HAL_outb(drv->tf_port + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

   // Read sectors
   for (uint32_t sec = 0; sec < count; sec++)
   {
      // Wait for data ready or error
      if (ata_wait_drq(drv->tf_port) != 0)
      {
         return -1;
      }

      // Read 512 bytes (256 words) from data port using 16-bit reads
      uint8_t *dest = buffer + (sec * 512);
      uint16_t *dest_words = (uint16_t *)dest;
      for (int i = 0; i < 256; i++)
      {
         // Read 16-bit word from data port
         dest_words[i] = HAL_inw(drv->tf_port + ATA_REG_DATA);
      }
   }

   return 0;
}

/**
 * Write sectors to ATA drive using PIO mode (28-bit LBA)
 */
int ATA_Write(int channel, int drive, uint32_t lba, const uint8_t *buffer,
              uint32_t count)
{
   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv || !buffer || count == 0) return -1;

   // Limit to 255 sectors per write (8-bit sector count)
   if (count > 255)
   {
      count = 255;
   }

   // Wait for drive to be ready
   if (ata_wait_busy(drv->tf_port) != 0)
   {
      return -1;
   }

   // Prepare device register value with master/slave bits, LBA flag, and upper
   // LBA bits (bits 24-27)
   uint8_t device = drv->slave_bits | 0x40 | ((lba >> 24) & 0x0F);

   // Write all command registers in the correct sequence
   HAL_outb(drv->tf_port + ATA_REG_NSECTOR, count & 0xFF);
   HAL_outb(drv->tf_port + ATA_REG_LBA_LOW, (lba & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_LBA_MID, ((lba >> 8) & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_LBA_HIGH, ((lba >> 16) & 0xFF));
   HAL_outb(drv->tf_port + ATA_REG_DEVICE, device);

   // Small delay to allow registers to settle
   for (volatile int i = 0; i < 50000; i++);

   // Issue WRITE SECTORS command
   HAL_outb(drv->tf_port + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

   // Write sectors
   for (uint32_t sec = 0; sec < count; sec++)
   {
      // Wait for drive ready to accept data
      if (ata_wait_drq(drv->tf_port) != 0)
      {
         return -1;
      }

      // Write 512 bytes (256 words) to data port using 16-bit writes
      const uint8_t *src = buffer + (sec * 512);
      const uint16_t *src_words = (const uint16_t *)src;
      for (int i = 0; i < 256; i++)
      {
         // Write 16-bit word to data port
         HAL_outw(drv->tf_port + ATA_REG_DATA, src_words[i]);
      }

      // For all sectors except the last, wait briefly before next sector
      // For the last sector, wait for completion
      if (sec < count - 1)
      {
         // Brief delay between sectors
         for (volatile int i = 0; i < 10000; i++);
      }
      else
      {
         // Last sector: wait for drive to finish
         if (ata_wait_busy(drv->tf_port) != 0)
         {
            return -1;
         }
      }
   }

   // Final status check to catch any errors
   uint8_t final_status = HAL_inb(drv->tf_port + ATA_REG_STATUS);
   if (final_status & ATA_STATUS_ERR)
   {
      uint8_t error = HAL_inb(drv->tf_port + ATA_REG_ERROR);
      return -1;
   }

   return 0;
}

/**
 * Perform software reset on ATA channel
 */
void ATA_Reset(int channel)
{
   uint16_t dcr_port = (channel == 0) ? 0x3F6 : 0x376;
   ata_soft_reset(dcr_port);
}

/**
 * Identify ATA drive
 */
int ATA_Identify(int channel, int drive, uint16_t *buffer)
{
   ata_driver_t *driver = (channel == 0 && drive == 0) ? &primary_master
                          : (channel == 0 && drive == 1)
                              ? &primary_slave
                              : NULL; // Add secondary if needed
   if (!driver) return -1;

   // Select drive
   HAL_outb(driver->tf_port + ATA_REG_DEVICE,
            driver->slave_bits | ((drive & 1) << 4));

   // Wait for drive to be ready
   ata_wait_for_ready(driver->tf_port);

   // Send IDENTIFY command
   HAL_outb(driver->tf_port + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

   // Wait for data
   if (ata_wait_for_data(driver->tf_port) != 0) return -1;

   // Read 256 words
   for (int i = 0; i < 256; i++)
   {
      buffer[i] = HAL_inw(driver->tf_port + ATA_REG_DATA);
   }

   return 0;
}

/**
 * Scan for ATA disks
 */
int ATA_Scan(DISK *disks, int maxDisks)
{
   int count = 0;

   // Scan all 4 possible ATA devices:
   // Channel 0 (Primary), Drive 0 (Master)
   // Channel 0 (Primary), Drive 1 (Slave)
   // Channel 1 (Secondary), Drive 0 (Master)
   // Channel 1 (Secondary), Drive 1 (Slave)
   int driveStartIndex = 0x80;
   for (int i = 0; i < MAX_DISKS; i++)
   {
      // Check if the disk pointer is valid first
      if (g_SysInfo->volume[i].disk == NULL) continue;

      // Skip floppy drives (0x00-0x7F)
      if (g_SysInfo->volume[i].disk->id < 0x80) continue;

      // Found a hard drive, increment start index
      driveStartIndex++;
   }

   for (int ch = 0; ch < 2; ch++)
   {
      for (int dr = 0; dr < 2; dr++)
      {
         if (count >= maxDisks) break;

         // Attempt to initialize the controller/drive
         // We pass 0 for partition info as we are just probing
         if (ATA_Init(ch, dr, 0, 0) != 0)
         {
            continue;
         }

         uint16_t identify_buffer[256];
         if (ATA_Identify(ch, dr, identify_buffer) == 0)
         {
            disks[count].id =
                driveStartIndex + count; // Assign BIOS-style ID (0x80, 0x81...)
            disks[count].type = 1;       // DISK_TYPE_ATA

            // Extract model name (words 27-46, 40 chars)
            for (int i = 0; i < 20; i++)
            {
               uint16_t word = identify_buffer[27 + i];
               disks[count].brand[i * 2] = (word >> 8) & 0xFF;
               disks[count].brand[i * 2 + 1] = word & 0xFF;
            }
            disks[count].brand[40] = '\0';
            // Trim trailing spaces
            for (int i = 39; i >= 0; i--)
            {
               if (disks[count].brand[i] == ' ')
                  disks[count].brand[i] = '\0';
               else
                  break;
            }

            // Extract size: Use LBA48 if supported (words 100-103), else CHS
            uint64_t total_sectors = 0;
            if (identify_buffer[83] & (1 << 10))
            { // LBA48 supported
               total_sectors = ((uint64_t)identify_buffer[103] << 48) |
                               ((uint64_t)identify_buffer[102] << 32) |
                               ((uint64_t)identify_buffer[101] << 16) |
                               identify_buffer[100];
            }
            else
            {
               total_sectors =
                   identify_buffer[60] | ((uint32_t)identify_buffer[61] << 16);
            }
            disks[count].size = total_sectors * 512; // Sector size is 512 bytes

            logfmt(LOG_INFO, "[DISK] Found ATA disk: ID=0x%x, Type=%u, Brand='%s', "
                   "Size=%llu bytes (Ch%d/Dr%d)\n",
                   disks[count].id, disks[count].type, disks[count].brand,
                   disks[count].size, ch, dr);
            count++;
         }
      }
   }
   return count;
}
