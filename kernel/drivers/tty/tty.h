// SPDX-License-Identifier: GPL-3.0-only

#ifndef TTY_H
#define TTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration for devfs */
struct DEVFS_DeviceNode;

/*
 * TTY System - Linux-like terminal device support
 *
 * Supports multiple TTY instances with:
 * - Canonical (cooked) and raw input modes
 * - Echo control
 * - Line editing
 * - Special character handling (CTRL+C, CTRL+D, etc.)
 * - Per-TTY input/output buffers
 */

/* Screen dimensions */
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

/* Buffer sizes */
#define TTY_INPUT_SIZE 4096
#define TTY_LINE_SIZE 256
#define TTY_SCROLLBACK 1000 /* Lines of scrollback */

/* Maximum TTY instances */
#define TTY_MAX_DEVICES 8

/* TTY flags (termios-like) */
#define TTY_FLAG_ECHO 0x0001   /* Echo input characters */
#define TTY_FLAG_ICANON 0x0002 /* Canonical mode (line editing) */
#define TTY_FLAG_ISIG 0x0004   /* Enable signals (CTRL+C, etc.) */
#define TTY_FLAG_ICRNL 0x0008  /* Map CR to NL on input */
#define TTY_FLAG_ONLCR 0x0010  /* Map NL to CR-NL on output */
#define TTY_FLAG_OPOST 0x0020  /* Output processing */

/* Default flags */
#define TTY_DEFAULT_FLAGS                                                      \
   (TTY_FLAG_ECHO | TTY_FLAG_ICANON | TTY_FLAG_ISIG | TTY_FLAG_ICRNL |         \
    TTY_FLAG_ONLCR | TTY_FLAG_OPOST)

/* Special characters */
#define TTY_CHAR_EOF 0x04    /* CTRL+D */
#define TTY_CHAR_INTR 0x03   /* CTRL+C */
#define TTY_CHAR_ERASE 0x7F  /* DEL / Backspace */
#define TTY_CHAR_WERASE 0x17 /* CTRL+W - erase word */
#define TTY_CHAR_KILL 0x15   /* CTRL+U - kill line */
#define TTY_CHAR_SUSP 0x1A   /* CTRL+Z */

/* Ioctl commands */
#define TTY_IOCTL_GETFLAGS 0x0001
#define TTY_IOCTL_SETFLAGS 0x0002
#define TTY_IOCTL_FLUSH 0x0003
#define TTY_IOCTL_GETSIZE 0x0004

/* Circular buffer structure */
typedef struct
{
   char *data;
   uint32_t size;
   volatile uint32_t head;  /* Read position */
   volatile uint32_t tail;  /* Write position */
   volatile uint32_t count; /* Number of bytes in buffer */
} TTY_Buffer;

/* TTY device structure */
typedef struct TTY_Device
{
   /* Device identification */
   uint32_t id; /* TTY number (0, 1, 2, ...) */
   bool active; /* Is this TTY slot in use */

   /* Input handling */
   TTY_Buffer input;             /* Cooked input (after line discipline) */
   char line_buf[TTY_LINE_SIZE]; /* Line editing buffer */
   uint32_t line_pos;            /* Current position in line buffer */
   uint32_t line_len;            /* Length of current line */
   bool line_ready;              /* A complete line is ready */
   bool eof_pending;             /* EOF was received */

   /* Output/display - scrollback buffer */
   char (*screen_buf)[SCREEN_WIDTH]; /* [TTY_SCROLLBACK][SCREEN_WIDTH] */
   uint16_t *display_buf;            /* VGA display buffer */
   uint32_t buf_head;                /* First line index in circular buffer */
   uint32_t buf_lines;               /* Number of lines used */
   uint32_t scroll_offset;           /* Lines scrolled back from bottom */

   /* Cursor */
   int cursor_x;
   int cursor_y;

   /* Attributes */
   uint8_t color;         /* Current color attribute */
   uint8_t default_color; /* Default color */

   /* Flags/modes */
   uint32_t flags; /* TTY_FLAG_* */

   /* ANSI state machine */
   int ansi_state;
   int ansi_params[16];
   int ansi_param_count;

   /* Dirty tracking for efficient repaint */
   int dirty_start;
   int dirty_end;

   /* Statistics */
   uint32_t bytes_read;
   uint32_t bytes_written;
} TTY_Device;

/*
 * TTY Subsystem API
 */

/* Initialize the TTY subsystem */
void TTY_Initialize(void);

/* Create/destroy TTY devices */
TTY_Device *TTY_Create(uint32_t id);
void TTY_Destroy(TTY_Device *tty);

/* Get TTY device by ID */
TTY_Device *TTY_GetDevice(void); /* Get current/active TTY */
TTY_Device *TTY_GetDeviceById(uint32_t id);
void TTY_SetActive(TTY_Device *tty);

/* Input functions (called by keyboard driver) */
void TTY_InputChar(TTY_Device *tty, char c);
void TTY_InputPush(char c); /* Push to active TTY */

/* Output functions */
void TTY_Write(TTY_Device *tty, const char *data, size_t len);
void TTY_WriteChar(TTY_Device *tty, char c);
void TTY_PutChar(char c); /* Write to active TTY */
void TTY_PutString(const char *s);

/* Reading (for processes) */
int TTY_Read(TTY_Device *tty, char *buf, size_t count);
int TTY_ReadNonBlock(TTY_Device *tty, char *buf, size_t count);
int TTY_ReadChar(void); /* Read from active TTY (legacy) */

/* Display control */
void TTY_ClearDevice(TTY_Device *tty);
void TTY_Clear(void);
void TTY_Scroll(int lines);
void TTY_Repaint(TTY_Device *tty);
void TTY_Flush(TTY_Device *tty);

/* Cursor control */
void TTY_SetCursor(TTY_Device *tty, int x, int y);
void TTY_GetCursor(TTY_Device *tty, int *x, int *y);

/* Attributes */
void TTY_SetColor(uint8_t color);
void TTY_SetFlags(TTY_Device *tty, uint32_t flags);
uint32_t TTY_GetFlags(TTY_Device *tty);

/* Mode helpers */
static inline bool TTY_IsCanonical(TTY_Device *tty)
{
   return (tty->flags & TTY_FLAG_ICANON) != 0;
}
static inline bool TTY_IsEcho(TTY_Device *tty)
{
   return (tty->flags & TTY_FLAG_ECHO) != 0;
}

/* Query functions */
int TTY_GetVisibleLineLength(int y);
int TTY_GetMaxScroll(void);
uint32_t TTY_GetVisibleStart(void);

/* Devfs operations */
uint32_t TTY_DevfsRead(struct DEVFS_DeviceNode *node, uint32_t offset,
                       uint32_t size, void *buffer);
uint32_t TTY_DevfsWrite(struct DEVFS_DeviceNode *node, uint32_t offset,
                        uint32_t size, const void *buffer);
int TTY_DevfsIoctl(struct DEVFS_DeviceNode *node, uint32_t cmd, void *arg);

#endif /* TTY_H */
