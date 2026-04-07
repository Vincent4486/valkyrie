// SPDX-License-Identifier: GPL-3.0-only

#include "ata.h"
#include <fs/devfs/devfs.h>
#include <hal/io.h>
#include <std/stdio.h>
#include <stdint.h>
#include <sys/sys.h>
#include <valecium/system.h>

static DEVFS_DeviceOps disk_ops = {.read = DISK_DevfsRead,
                                   .write = DISK_DevfsWrite};

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

/*
 * Floating-bus sentinel: an 8-bit Status Register read returns 0xFF when no
 * drive is connected (the pull-up resistors on the data bus are undriven).
 * 86Box models this accurately – QEMU returns 0x00 on empty channels.
 */
#define ATA_FLOATING_BUS 0xFF

// Driver data structure
typedef struct
{
   uint32_t partition_length; /* Total sectors – populated by ATA_Init via
                                 IDENTIFY */
   uint32_t start_lba; /* Partition start (absolute LBA; 0 = start of disk)   */
   uint16_t dcr_port;  /* Alt-Status / Device Control port                    */
   uint16_t tf_port;   /* Task-file base port                                 */
   uint8_t slave_bits; /* Drive-select byte: 0xA0 (master) or 0xB0 (slave)   */
} ata_driver_t;

/*
 * Static driver descriptors.
 * partition_length starts at 0 and is filled in by ATA_Init from the
 * IDENTIFY device response – no hardcoded geometry or size anywhere.
 */
static ata_driver_t primary_master = {
    .dcr_port = 0x3F6, .tf_port = 0x1F0, .slave_bits = 0xA0};
static ata_driver_t primary_slave = {
    .dcr_port = 0x3F6, .tf_port = 0x1F0, .slave_bits = 0xB0};
static ata_driver_t secondary_master = {
    .dcr_port = 0x376, .tf_port = 0x170, .slave_bits = 0xA0};
static ata_driver_t secondary_slave = {
    .dcr_port = 0x376, .tf_port = 0x170, .slave_bits = 0xB0};

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
 * ata_400ns_delay – generate an ~400 ns bus delay by issuing five reads of the
 * Alternate Status register (DCR/Alt-Status port = tf_port + 0x206 offset, but
 * in practice the driver passes dcr_port directly).  Each I/O read on the ISA
 * bus takes roughly 100 ns on PIIX4/86Box, so five reads ≥ 400 ns.
 *
 * Must be called after writing the DEVICE register and before polling BSY, as
 * the drive is allowed up to 400 ns to assert BSY after being selected.
 */
static inline void ata_400ns_delay(uint16_t dcr_port)
{
   /* The DCR register doubles as the Alt Status port when read. */
   (void)g_HalIoOperations->inb(dcr_port); /* read 1 */
   (void)g_HalIoOperations->inb(dcr_port); /* read 2 */
   (void)g_HalIoOperations->inb(dcr_port); /* read 3 */
   (void)g_HalIoOperations->inb(dcr_port); /* read 4 */
   (void)g_HalIoOperations->inb(dcr_port); /* read 5 */
}

/**
 * ata_is_floating_bus – return 1 if the channel is unpopulated.
 *
 * On a real ISA/PIIX4 bus (and in 86Box high-accuracy mode) an empty channel
 * drives 0xFF on all data lines due to pull-up resistors.  Reading the Status
 * register and finding 0xFF means there is no device attached; attempting to
 * IDENTIFY such a channel would spin indefinitely waiting for BSY to clear.
 */
static inline int ata_is_floating_bus(uint16_t tf_port)
{
   uint8_t status = g_HalIoOperations->inb(tf_port + ATA_REG_STATUS);
   return (status == ATA_FLOATING_BUS);
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
      uint8_t status = g_HalIoOperations->inb(tf_port + ATA_REG_STATUS);
      /* Treat a floating bus (0xFF) as a hard error to avoid spinning */
      if (status == ATA_FLOATING_BUS) return -1;
      if (!(status & ATA_STATUS_BSY)) return 0;

      // Small delay to prevent bus saturation
      for (volatile int i = 0; i < 100; i++);
   }

   return -1; // Timeout
}

/**
 * ata_wait_drq – wait until BSY is clear AND DRQ is set.
 *
 * 86Box / PIIX4 strict polling order (OSDev ATA PIO guide, §4.3):
 *  1. Spin while BSY=1.  Do NOT test DRQ while BSY is still set.
 *  2. Once BSY=0, check ERR/DF first, then confirm DRQ=1.
 *
 * The previous implementation tested DRQ without checking BSY first, which
 * could cause premature reads on high-accuracy emulators.
 */
static int ata_wait_drq(uint16_t tf_port)
{
   int timeout = 10000;

   while (timeout--)
   {
      uint8_t status = g_HalIoOperations->inb(tf_port + ATA_REG_STATUS);

      /* Floating bus – no drive present */
      if (status == ATA_FLOATING_BUS) return -1;

      /* Step 1: BSY must clear before we trust other bits */
      if (status & ATA_STATUS_BSY)
      {
         for (volatile int i = 0; i < 100; i++);
         continue;
      }

      /* Step 2: BSY=0 – now safe to inspect ERR and DRQ */
      if (status & ATA_STATUS_ERR) return -1;
      if (status & ATA_STATUS_DRQ) return 0;

      for (volatile int i = 0; i < 100; i++);
   }

   return -1; /* Timeout */
}

/**
 * Wait for drive to be ready (not busy and DRDY set)
 */
static int ata_wait_for_ready(uint16_t tf_port)
{
   int timeout = 10000;

   while (timeout--)
   {
      uint8_t status = g_HalIoOperations->inb(tf_port + ATA_REG_STATUS);
      if (!(status & ATA_STATUS_BSY) && (status & ATA_STATUS_DRDY)) return 0;

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
   g_HalIoOperations->outb(dcr_port, 0x04);

   // Wait a bit
   for (volatile int i = 0; i < 100000; i++);

   // Clear SRST bit
   g_HalIoOperations->outb(dcr_port, 0x00);

   // Wait for reset to complete
   for (volatile int i = 0; i < 100000; i++);
}

/**
 * ATA_Init – reset the channel and populate the driver descriptor.
 *
 * If partition_size is non-zero it is used as-is (caller already knows the
 * extent, e.g. from a partition table).  If it is zero, the total sector
 * count is read from the IDENTIFY DEVICE response so that no geometry is
 * ever hardcoded here.
 */
int ATA_Init(int channel, int drive, uint32_t partition_start,
             uint32_t partition_size)
{
   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv) return -1;

   /* Bail immediately on a floating bus – no device present. */
   if (ata_is_floating_bus(drv->tf_port)) return -1;

   /* Software reset; then poll until BSY clears (mandatory post-reset wait). */
   ata_soft_reset(drv->dcr_port);
   if (ata_wait_busy(drv->tf_port) != 0) return -1;

   drv->start_lba = partition_start;

   if (partition_size != 0)
   {
      /* Caller-supplied size – use it directly. */
      drv->partition_length = partition_size;
   }
   else
   {
      /*
       * Auto-detect total sector count from IDENTIFY DEVICE.
       *
       * Protocol (OSDev ATA PIO):
       *  1. Write DEVICE register (drive select + LBA flag).
       *  2. Wait ≥ 400 ns for the drive to assert BSY.
       *  3. Wait for DRDY=1 / BSY=0.
       *  4. Issue IDENTIFY (0xEC) and wait for DRQ.
       *  5. Read 256 words.
       */
      uint16_t id[256];

      g_HalIoOperations->outb(drv->tf_port + ATA_REG_DEVICE, drv->slave_bits);
      ata_400ns_delay(drv->dcr_port);
      if (ata_wait_for_ready(drv->tf_port) != 0) return -1;

      g_HalIoOperations->outb(drv->tf_port + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
      if (ata_wait_drq(drv->tf_port) != 0) return -1;

      for (int i = 0; i < 256; i++)
         id[i] = g_HalIoOperations->inw(drv->tf_port + ATA_REG_DATA);

      if (id[83] & (1u << 10))
      {
         /* LBA48: words 100–103 hold the 48-bit sector count.             */
         drv->partition_length =
             (uint32_t)(((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32) |
                        ((uint64_t)id[101] << 16) | (uint64_t)id[100]);
      }
      else
      {
         /* LBA28: words 60–61 hold the 28-bit sector count (little-endian). */
         drv->partition_length = (uint32_t)id[60] | ((uint32_t)id[61] << 16);
      }
   }

   return 0;
}

/**
 * Read sectors from ATA drive using PIO mode (28-bit LBA)
 */
int ATA_Read(DISK *disk, uint32_t lba, uint8_t *buffer, uint32_t count)
{
   /* Validate inputs and ensure private driver data exists */
   if (!disk || !disk->private || !buffer || count == 0) return -1;
   if (disk->type != DISK_TYPE_ATA) return -1;

   ATA_DISK *priv = (ATA_DISK *)disk->private;
   int channel = priv->channel;
   int drive = priv->drive;
   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv) return -1;

   // Limit to 255 sectors per read (8-bit sector count)
   if (count > 255) count = 255;

   /*
    * ATA drive-select sequence (OSDev ATA PIO, §3.2):
    *  1. Write DEVICE first – selects master/slave and latches LBA bits 24-27.
    *  2. Wait ≥ 400 ns for the drive to assert BSY.
    *  3. Poll until BSY=0, DRDY=1 before writing the remaining task-file regs.
    *  4. Write NSector, LBA_LOW/MID/HIGH, then issue the command.
    *
    * The LBA bit (0x40) must be set in the DEVICE byte; without it the device
    * interprets the address as CHS and may ABRT the command.
    */
   uint8_t device = drv->slave_bits | 0x40 | ((lba >> 24) & 0x0F);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_DEVICE, device);
   ata_400ns_delay(drv->dcr_port);
   if (ata_wait_for_ready(drv->tf_port) != 0) return -1;

   g_HalIoOperations->outb(drv->tf_port + ATA_REG_NSECTOR, count & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_LOW, lba & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

   // Issue READ SECTORS command
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

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
         dest_words[i] = g_HalIoOperations->inw(drv->tf_port + ATA_REG_DATA);
      }
   }

   return 0;
}

/**
 * Write sectors to ATA drive using PIO mode (28-bit LBA)
 */
int ATA_Write(DISK *disk, uint32_t lba, const uint8_t *buffer, uint32_t count)
{
   /* Validate inputs and ensure private driver data exists */
   if (!disk || !disk->private || !buffer || count == 0) return -1;
   if (disk->type != DISK_TYPE_ATA) return -1;

   ATA_DISK *priv = (ATA_DISK *)disk->private;
   int channel = priv->channel;
   int drive = priv->drive;

   ata_driver_t *drv = ata_get_driver(channel, drive);
   if (!drv) return -1;

   // Limit to 255 sectors per write (8-bit sector count)
   if (count > 255) count = 255;

   /* Same drive-select sequence as ATA_Read – DEVICE register first. */
   uint8_t device = drv->slave_bits | 0x40 | ((lba >> 24) & 0x0F);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_DEVICE, device);
   ata_400ns_delay(drv->dcr_port);
   if (ata_wait_for_ready(drv->tf_port) != 0) return -1;

   g_HalIoOperations->outb(drv->tf_port + ATA_REG_NSECTOR, count & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_LOW, lba & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

   // Issue WRITE SECTORS command
   g_HalIoOperations->outb(drv->tf_port + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

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
         g_HalIoOperations->outw(drv->tf_port + ATA_REG_DATA, src_words[i]);
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
   uint8_t final_status = g_HalIoOperations->inb(drv->tf_port + ATA_REG_STATUS);
   if (final_status & ATA_STATUS_ERR)
   {
      /* Read and log the error register to determine the cause */
      uint8_t err = g_HalIoOperations->inb(drv->tf_port + ATA_REG_ERROR);
      logfmt(LOG_ERROR, "[ATA] Write error: status=0x%02x error=0x%02x\n",
             final_status, err);
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
 * ATA_Identify – issue IDENTIFY DEVICE and return the 256-word response.
 *
 * Drive-select uses slave_bits directly (already encodes the master/slave
 * bit – no need to re-OR the drive index, which would corrupt bit 4).
 * A 400 ns delay follows the device-register write before polling DRDY.
 */
int ATA_Identify(int channel, int drive, uint16_t *buffer)
{
   ata_driver_t *driver = ata_get_driver(channel, drive);
   if (!driver) return -1;

   /* Guard against empty / floating channel. */
   if (ata_is_floating_bus(driver->tf_port)) return -1;

   /* Select drive, wait for it to acknowledge the selection. */
   g_HalIoOperations->outb(driver->tf_port + ATA_REG_DEVICE,
                           driver->slave_bits);
   ata_400ns_delay(driver->dcr_port);
   if (ata_wait_for_ready(driver->tf_port) != 0) return -1;

   /* Issue IDENTIFY and wait for the data buffer to fill. */
   g_HalIoOperations->outb(driver->tf_port + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
   if (ata_wait_drq(driver->tf_port) != 0) return -1;

   /*
    * Read 256 words.
    * ATA IDENTIFY stores ASCII strings in word-swapped order: within each
    * 16-bit word the high byte is the first character.  Callers that extract
    * model/serial strings must swap accordingly (see ATA_Scan below).
    */
   for (int i = 0; i < 256; i++)
      buffer[i] = g_HalIoOperations->inw(driver->tf_port + ATA_REG_DATA);

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
            ATA_DISK *private = kmalloc(sizeof(ATA_DISK));
            if (!private)
            {
               logfmt(LOG_ERROR,
                      "[DISK] Failed to allocate ATA_DISK for ch%d dr%d\n", ch,
                      dr);
               continue;
            }
            private->channel = ch;
            private->drive = dr;

            /* BIOS-style drive ID: 0x80=primary-master, 0x81=primary-slave,
             * 0x82=secondary-master, 0x83=secondary-slave */
            disks[count].id = (uint8_t)(0x80 + ch * 2 + dr);
            disks[count].type = 1; // DISK_TYPE_ATA

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
            disks[count].private = private;

            logfmt(LOG_INFO,
                   "[DISK] Found ATA disk: ID=0x%x, Type=%u, Brand='%s', "
                   "Size=%llu bytes (Ch%d/Dr%d)\n",
                   disks[count].id, disks[count].type, disks[count].brand,
                   disks[count].size, ch, dr);

            /* Register the disk device in devfs
             * Device naming: hda, hdb, hdc, hdd for primary/secondary
             * master/slave
             */
            char devname[8];
            devname[0] = 'h';
            devname[1] = 'd';
            devname[2] = 'a' + (ch * 2) + dr; /* hda=0, hdb=1, hdc=2, hdd=3 */
            devname[3] = '\0';

            /* Major 3 for IDE disks, minor = disk index */
            uint32_t disk_size = (uint32_t)(disks[count].size & 0xFFFFFFFF);
            DEVFS_RegisterDevice(devname, DEVFS_TYPE_BLOCK, 3, count, disk_size,
                                 &disk_ops, &disks[count]);

            count++;
         }
      }
   }
   return count;
}
