// SPDX-License-Identifier: GPL-3.0-only

#include "tty.h"
#include <cpu/process.h>
#include <fs/devfs/devfs.h>
#include <hal/scheduler.h>
#include <hal/video.h>
#include <mem/mm_kernel.h>

/* TTY device array and active terminal */
static TTY_Device *g_TTYDevices[TTY_MAX_DEVICES];
static TTY_Device *g_ActiveTTY = NULL;
static bool g_TTYInitialized = false;

/* Static BSS input storage for each TTY. */
static char g_TTYInputBufs[TTY_MAX_DEVICES][TTY_INPUT_SIZE];

static void buffer_init(TTY_Buffer *buf, char *data, uint32_t size)
{
   buf->data = data;
   buf->size = size;
   buf->head = 0;
   buf->tail = 0;
   buf->count = 0;
}

static int buffer_push(TTY_Buffer *buf, char c)
{
   if (buf->count >= buf->size) return -1;
   buf->data[buf->tail] = c;
   buf->tail = (buf->tail + 1) % buf->size;
   buf->count++;
   return 0;
}

static int buffer_pop(TTY_Buffer *buf)
{
   if (buf->count == 0) return -1;
   char c = buf->data[buf->head];
   buf->head = (buf->head + 1) % buf->size;
   buf->count--;
   return (int)(unsigned char)c;
}

static void buffer_clear(TTY_Buffer *buf)
{
   buf->head = 0;
   buf->tail = 0;
   buf->count = 0;
}

static void tty_sync_cursor_from_backend(TTY_Device *tty)
{
   const HAL_VideoOperations *vdev = g_HalVideoOperations;
   if (!tty || tty != g_ActiveTTY || !vdev || !vdev->GetCursor) return;
   vdev->GetCursor(&tty->cursor_x, &tty->cursor_y);
}

static void tty_emit_to_display(TTY_Device *tty, char c)
{
   const HAL_VideoOperations *vdev = g_HalVideoOperations;
   if (!tty || tty != g_ActiveTTY || !vdev || !vdev->PutChar) return;

   /* x/y < 0 selects backend stream mode (ANSI + wrapping handled by VGA). */
   vdev->PutChar(c, tty->color, -1, -1);
   tty_sync_cursor_from_backend(tty);
}

static void line_flush(TTY_Device *tty)
{
   for (uint32_t i = 0; i < tty->line_len; i++)
   {
      buffer_push(&tty->input, tty->line_buf[i]);
   }
   buffer_push(&tty->input, '\n');
   tty->line_len = 0;
   tty->line_pos = 0;
   tty->line_ready = true;

   Process_WakeByChannel(tty);
}

static int tty_has_pending_read(TTY_Device *tty)
{
   if (!tty) return -1;

   if (tty->eof_pending && tty->input.count == 0)
   {
      return 0;
   }

   if (TTY_IsCanonical(tty) == 0)
   {
      if (tty->line_ready) return 0;
      if (tty->input.count > 0) return 0;
      return -1;
   }

   return (tty->input.count > 0) ? 0 : -1;
}

static void line_erase_char(TTY_Device *tty)
{
   if (tty->line_len == 0) return;

   tty->line_len--;
   if (tty->line_pos > tty->line_len)
   {
      tty->line_pos = tty->line_len;
   }

   if (TTY_IsEcho(tty) == 0)
   {
      /* Linux-like erase echo sequence. */
      TTY_WriteChar(tty, '\b');
      TTY_WriteChar(tty, ' ');
      TTY_WriteChar(tty, '\b');
   }
}

static void line_kill(TTY_Device *tty)
{
   while (tty->line_len > 0)
   {
      line_erase_char(tty);
   }
}

static void line_add_char(TTY_Device *tty, char c)
{
   if (tty->line_len >= TTY_LINE_SIZE - 1) return;

   tty->line_buf[tty->line_len++] = c;
   tty->line_pos = tty->line_len;
   if (TTY_IsEcho(tty) == 0)
   {
      TTY_WriteChar(tty, c);
   }
}

static void tty_input_noecho(TTY_Device *tty, char c)
{
   if (!tty) return;

   buffer_push(&tty->input, c);
   Process_WakeByChannel(tty);
}

void TTY_Initialize(void)
{
   if (g_TTYInitialized) return;

   for (int i = 0; i < TTY_MAX_DEVICES; i++)
   {
      g_TTYDevices[i] = NULL;
   }

   TTY_Device *tty0 = TTY_Create(0);
   if (tty0)
   {
      g_ActiveTTY = tty0;
   }

   g_TTYInitialized = true;
   TTY_Clear();
}

TTY_Device *TTY_Create(uint32_t id)
{
   if (id >= TTY_MAX_DEVICES) return NULL;
   if (g_TTYDevices[id] != NULL) return g_TTYDevices[id];

   TTY_Device *tty = (TTY_Device *)kzalloc(sizeof(TTY_Device));
   if (!tty) return NULL;

   buffer_init(&tty->input, g_TTYInputBufs[id], TTY_INPUT_SIZE);

   tty->id = id;
   tty->active = true;
   tty->line_len = 0;
   tty->line_pos = 0;
   tty->line_ready = false;
   tty->eof_pending = false;
   tty->cursor_x = 0;
   tty->cursor_y = 0;
   tty->color = 0x07;
   tty->default_color = 0x07;
   tty->flags = TTY_DEFAULT_FLAGS;
   tty->cols = SCREEN_WIDTH;
   tty->rows = SCREEN_HEIGHT;
   tty->bytes_read = 0;
   tty->bytes_written = 0;

   g_TTYDevices[id] = tty;
   return tty;
}

void TTY_Destroy(TTY_Device *tty)
{
   if (!tty || tty->id >= TTY_MAX_DEVICES) return;

   g_TTYDevices[tty->id] = NULL;
   if (g_ActiveTTY == tty) g_ActiveTTY = g_TTYDevices[0];

   free(tty);
}

TTY_Device *TTY_GetDevice(void) { return g_ActiveTTY; }

TTY_Device *TTY_GetDeviceById(uint32_t id)
{
   if (id >= TTY_MAX_DEVICES) return NULL;
   return g_TTYDevices[id];
}

void TTY_SetActive(TTY_Device *tty)
{
   if (!tty) return;
   g_ActiveTTY = tty;

   const HAL_VideoOperations *vdev = g_HalVideoOperations;
   if (vdev && vdev->SetCursor)
   {
      vdev->SetCursor(tty->cursor_x, tty->cursor_y);
      tty_sync_cursor_from_backend(tty);
   }
}

void TTY_InputChar(TTY_Device *tty, char c)
{
   if (!tty) return;

   if ((tty->flags & TTY_FLAG_ICRNL) && c == '\r')
   {
      c = '\n';
   }

   if (tty->flags & TTY_FLAG_ISIG)
   {
      if (c == TTY_CHAR_INTR)
      {
         /* TODO: Send SIGINT to foreground process. */
         if (TTY_IsEcho(tty) == 0)
         {
            TTY_WriteChar(tty, '^');
            TTY_WriteChar(tty, 'C');
            TTY_WriteChar(tty, '\n');
         }
         tty->line_len = 0;
         tty->line_pos = 0;
         return;
      }

      if (c == TTY_CHAR_EOF)
      {
         if (tty->line_len == 0)
         {
            tty->eof_pending = true;
            tty->line_ready = true;
            Process_WakeByChannel(tty);
         }
         else
         {
            line_flush(tty);
         }
         return;
      }

      if (c == TTY_CHAR_SUSP)
      {
         /* TODO: Send SIGTSTP to foreground process. */
         if (TTY_IsEcho(tty) == 0)
         {
            TTY_WriteChar(tty, '^');
            TTY_WriteChar(tty, 'Z');
            TTY_WriteChar(tty, '\n');
         }
         return;
      }
   }

   if (TTY_IsCanonical(tty) == 0)
   {
      if (c == '\b' || c == TTY_CHAR_ERASE)
      {
         line_erase_char(tty);
         return;
      }

      if (c == TTY_CHAR_KILL)
      {
         line_kill(tty);
         return;
      }

      if (c == '\n')
      {
         if (TTY_IsEcho(tty) == 0)
         {
            TTY_WriteChar(tty, '\n');
         }
         line_flush(tty);
         return;
      }

      line_add_char(tty, c);
      return;
   }

   buffer_push(&tty->input, c);
   Process_WakeByChannel(tty);
   if (TTY_IsEcho(tty) == 0)
   {
      TTY_WriteChar(tty, c);
   }
}

void TTY_InputEscape(TTY_Device *tty, const char *seq)
{
   if (!tty || !seq) return;

   for (size_t i = 0; seq[i] != '\0'; ++i)
   {
      tty_input_noecho(tty, seq[i]);
   }
}

void TTY_InputArrow(TTY_Device *tty, char direction)
{
   if (!tty) return;

   if (direction != 'A' && direction != 'B' && direction != 'C' &&
       direction != 'D')
   {
      return;
   }

   tty_input_noecho(tty, '\x1B');
   tty_input_noecho(tty, '[');
   tty_input_noecho(tty, direction);
}

void TTY_WriteChar(TTY_Device *tty, char c)
{
   if (!tty) return;

   if ((tty->flags & TTY_FLAG_OPOST) && (tty->flags & TTY_FLAG_ONLCR) && c == '\n')
   {
      tty_emit_to_display(tty, '\r');
   }

   tty_emit_to_display(tty, c);
   tty->bytes_written++;
}

void TTY_Write(TTY_Device *tty, const char *data, size_t len)
{
   if (!tty || !data) return;

   for (size_t i = 0; i < len; i++)
   {
      TTY_WriteChar(tty, data[i]);
   }
}

int TTY_Read(TTY_Device *tty, char *buf, size_t count)
{
   if (!tty || !buf || count == 0) return 0;

   Process *current = Process_GetCurrent();
   while (tty_has_pending_read(tty) < 0)
   {
      if (!current) return 0;

      Process_BlockOn(current, tty);

      /* Avoid a lost wakeup by checking availability after blocking state is
       * visible to input IRQ handlers. */
      if (tty_has_pending_read(tty) == 0)
      {
         Process_Unblock(current);
         break;
      }

      if (!g_HalSchedulerOperations || !g_HalSchedulerOperations->ContextSwitch)
      {
         Process_Unblock(current);
         return 0;
      }

      g_HalSchedulerOperations->ContextSwitch();
      current = Process_GetCurrent();
      if (!current) return 0;
   }

   if (current) Process_Unblock(current);

   if (tty->eof_pending && tty->input.count == 0)
   {
      tty->eof_pending = false;
      tty->line_ready = false;
      return 0;
   }

   size_t bytes_read = 0;
   while (bytes_read < count)
   {
      int c = buffer_pop(&tty->input);
      if (c < 0) break;

      buf[bytes_read++] = (char)c;
      if (TTY_IsCanonical(tty) == 0 && c == '\n')
      {
         break;
      }
   }

   if (tty->input.count == 0)
   {
      tty->line_ready = false;
   }

   tty->bytes_read += bytes_read;
   return (int)bytes_read;
}

void TTY_ClearDevice(TTY_Device *tty)
{
   if (!tty) return;

   tty->cursor_x = 0;
   tty->cursor_y = 0;

   if (tty == g_ActiveTTY)
   {
      const HAL_VideoOperations *vdev = g_HalVideoOperations;
      if (vdev && vdev->Clear) vdev->Clear(tty->color);
      if (vdev && vdev->SetCursor) vdev->SetCursor(0, 0);
      tty_sync_cursor_from_backend(tty);
   }
}

void TTY_Clear(void)
{
   if (g_ActiveTTY)
   {
      TTY_ClearDevice(g_ActiveTTY);
   }
}

void TTY_SetCursor(TTY_Device *tty, int x, int y)
{
   if (!tty) return;

   if (x < 0) x = 0;
   if (x >= tty->cols) x = tty->cols - 1;
   if (y < 0) y = 0;
   if (y >= tty->rows) y = tty->rows - 1;

   tty->cursor_x = x;
   tty->cursor_y = y;

   if (tty == g_ActiveTTY)
   {
      const HAL_VideoOperations *vdev = g_HalVideoOperations;
      if (vdev && vdev->SetCursor) vdev->SetCursor(x, y);
      tty_sync_cursor_from_backend(tty);
   }
}

void TTY_GetCursor(TTY_Device *tty, int *x, int *y)
{
   if (!tty) return;

   tty_sync_cursor_from_backend(tty);
   if (x) *x = tty->cursor_x;
   if (y) *y = tty->cursor_y;
}

void TTY_SetColor(uint8_t color)
{
   if (g_ActiveTTY)
   {
      g_ActiveTTY->color = color;
   }
}

void TTY_SetFlags(TTY_Device *tty, uint32_t flags)
{
   if (!tty) return;
   tty->flags = flags;
}

uint32_t TTY_GetFlags(TTY_Device *tty)
{
   if (!tty) return 0;
   return tty->flags;
}

uint32_t TTY_DevfsRead(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                       void *buffer)
{
   (void)offset;
   if (!buffer || size == 0) return 0;

   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) tty = g_ActiveTTY;
   if (!tty) return 0;

   return (uint32_t)TTY_Read(tty, (char *)buffer, size);
}

uint32_t TTY_DevfsWrite(DEVFS_DeviceNode *node, uint32_t offset, uint32_t size,
                        const void *buffer)
{
   (void)offset;
   if (!buffer || size == 0) return 0;

   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) tty = g_ActiveTTY;
   if (!tty) return 0;

   TTY_Write(tty, (const char *)buffer, size);
   return size;
}

int TTY_DevfsIoctl(DEVFS_DeviceNode *node, uint32_t cmd, void *arg)
{
   TTY_Device *tty = node ? (TTY_Device *)node->private_data : g_ActiveTTY;
   if (!tty) return -1;

   switch (cmd)
   {
   case TTY_IOCTL_GETFLAGS:
      if (arg) *(uint32_t *)arg = tty->flags;
      return 0;

   case TTY_IOCTL_SETFLAGS:
      if (arg) tty->flags = *(uint32_t *)arg;
      return 0;

   case TTY_IOCTL_FLUSH:
      buffer_clear(&tty->input);
      tty->line_len = 0;
      tty->line_pos = 0;
      tty->line_ready = false;
      return 0;

   case TTY_IOCTL_GETSIZE:
      if (arg)
      {
         uint16_t *size = (uint16_t *)arg;
         size[0] = (uint16_t)tty->cols;
         size[1] = (uint16_t)tty->rows;
      }
      return 0;

   default:
      return -1;
   }
}
