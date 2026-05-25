// SPDX-License-Identifier: GPL-3.0-only

#include "video.h"
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Serial port (COM1) I/O registers                                   */
/* ------------------------------------------------------------------ */

#define SERIAL_PORT 0x3F8
#define SERIAL_DATA (SERIAL_PORT + 0) /* R/W: data register       */
#define SERIAL_IER (SERIAL_PORT + 1)  /* W:   interrupt enable    */
#define SERIAL_FCR (SERIAL_PORT + 2)  /* W:   FIFO control        */
#define SERIAL_LCR (SERIAL_PORT + 3)  /* W:   line control        */
#define SERIAL_MCR (SERIAL_PORT + 4)  /* W:   modem control       */
#define SERIAL_LSR (SERIAL_PORT + 5)  /* R:   line status         */

/* Line status register bits */
#define LSR_THR_EMPTY (1 << 5) /* Transmitter hold reg empty */

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static int s_Initialized = 0;

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int Serial_Initialize(void)
{
   /* Baud rate divisor = 1  (115200 baud with 1.8432 MHz crystal) */
   outb(SERIAL_LCR, 0x80);  /* DLAB = 1 (enable divisor access) */
   outb(SERIAL_DATA, 0x01); /* divisor low  byte                */
   outb(SERIAL_IER, 0x00);  /* divisor high byte                */

   outb(SERIAL_LCR, 0x03); /* 8 bits, no parity, 1 stop bit    */
   outb(SERIAL_FCR, 0xC7); /* enable FIFO, clear, 14-byte thr  */
   outb(SERIAL_MCR, 0x0B); /* DTR + RTS + OUT2 (enable IRQ)    */
   outb(SERIAL_IER, 0x00); /* disable all interrupts           */

   s_Initialized = 1;
   return SUCCESS;
}

int Serial_PutChar(char c, int x, int y, char color)
{
   (void)x;
   (void)y;
   (void)color;

   if (!s_Initialized) return -ENODEV;

   /* Wait until the transmitter holding register is empty */
   while (!(inb(SERIAL_LSR) & LSR_THR_EMPTY));

   outb(SERIAL_DATA, (uint8_t)c);
   return SUCCESS;
}

int Serial_PutPixel(int pixel, int x, int y)
{
   (void)pixel;
   (void)x;
   (void)y;
   return -EINVAL;
}
