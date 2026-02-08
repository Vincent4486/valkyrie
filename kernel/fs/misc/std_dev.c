// SPDX-License-Identifier: GPL-3.0-only

#include <fs/devfs/devfs.h>
#include <drivers/tty/tty.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stddef.h>
#include <mem/mm_kernel.h>

/*
 * Standard device implementations for devfs
 * These are the built-in devices like /dev/null, /dev/zero, etc.
 */

/* /dev/null - discards all writes, reads return EOF */
static uint32_t null_read(DEVFS_DeviceNode *node, uint32_t offset, 
                          uint32_t size, void *buffer)
{
   (void)node; (void)offset; (void)size; (void)buffer;
   return 0; /* EOF */
}

static uint32_t null_write(DEVFS_DeviceNode *node, uint32_t offset,
                           uint32_t size, const void *buffer)
{
   (void)node; (void)offset; (void)buffer;
   return size; /* Accept all writes */
}

const DEVFS_DeviceOps null_ops = {
   .read = null_read,
   .write = null_write,
   .ioctl = NULL,
   .close = NULL
};

/* /dev/zero - reads return zeros, writes are discarded */
static uint32_t zero_read(DEVFS_DeviceNode *node, uint32_t offset,
                          uint32_t size, void *buffer)
{
   (void)node; (void)offset;
   if (buffer && size > 0) {
      memset(buffer, 0, size);
   }
   return size;
}

const DEVFS_DeviceOps zero_ops = {
   .read = zero_read,
   .write = null_write, /* Same as null - discard */
   .ioctl = NULL,
   .close = NULL
};

/* /dev/full - reads return zeros, writes return ENOSPC */
static uint32_t full_write(DEVFS_DeviceNode *node, uint32_t offset,
                           uint32_t size, const void *buffer)
{
   (void)node; (void)offset; (void)size; (void)buffer;
   return 0; /* No space - return 0 bytes written */
}

const DEVFS_DeviceOps full_ops = {
   .read = zero_read,
   .write = full_write,
   .ioctl = NULL,
   .close = NULL
};

/* TTY device operations - use external functions from tty.c */
static const DEVFS_DeviceOps tty_ops = {
   .read = TTY_DevfsRead,
   .write = TTY_DevfsWrite,
   .ioctl = TTY_DevfsIoctl,
   .close = NULL
};

void register_standard_devices(void)
{
   /* Register /dev/null */
   DEVFS_RegisterDevice("null", DEVFS_TYPE_CHAR, 1, 3, 0, &null_ops, NULL);
   
   /* Register /dev/zero */
   DEVFS_RegisterDevice("zero", DEVFS_TYPE_CHAR, 1, 5, 0, &zero_ops, NULL);
   
   /* Register /dev/full */
   DEVFS_RegisterDevice("full", DEVFS_TYPE_CHAR, 1, 7, 0, &full_ops, NULL);
   
   /* Register /dev/tty - controlling terminal (uses active TTY) */
   DEVFS_RegisterDevice("tty", DEVFS_TYPE_CHAR, 5, 0, 0, &tty_ops, NULL);
   
   /* Register /dev/console - system console (TTY0) */
   TTY_Device *tty0 = TTY_GetDeviceById(0);
   DEVFS_RegisterDevice("console", DEVFS_TYPE_CHAR, 5, 1, 0, &tty_ops, tty0);
   
   /* Register /dev/tty0 through /dev/tty7 */
   for (uint32_t i = 0; i < TTY_MAX_DEVICES; i++) {
      char name[8];
      name[0] = 't';
      name[1] = 't';
      name[2] = 'y';
      name[3] = '0' + i;
      name[4] = '\0';
      
      TTY_Device *tty = TTY_GetDeviceById(i);
      if (!tty && i > 0) {
         tty = TTY_Create(i);  /* Create TTY on demand */
      }
      DEVFS_RegisterDevice(name, DEVFS_TYPE_CHAR, 4, i, 0, &tty_ops, tty);
   }
   
   logfmt(LOG_INFO, "[DEVFS] Registered standard devices (null, zero, full, tty, console)\n");
}
