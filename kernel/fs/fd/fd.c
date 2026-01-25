// SPDX-License-Identifier: GPL-3.0-only

#include "fd.h"
#include <cpu/process.h>
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <valkyrie/fs.h>

#include <drivers/tty/tty.h>

// Helper: Get file descriptor from process
FileDescriptor *FD_Get(void *proc_ptr, int fd)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc || fd < 0 || fd >= FD_TABLE_SIZE) return NULL;

   return proc->fd_table[fd];
}

// Helper: Find first free file descriptor
int FD_FindFree(void *proc_ptr)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc) return -1;

   // Reserve 0, 1, 2 for stdin, stdout, stderr
   for (int i = 3; i < FD_TABLE_SIZE; i++)
   {
      if (proc->fd_table[i] == NULL) return i;
   }

   return -1; // EMFILE (too many open files)
}

// Open a file and return file descriptor
int FD_Open(void *proc_ptr, const char *path, int flags)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc || !path) return -1; // EINVAL

   // Find free descriptor slot
   int fd = FD_FindFree(proc);
   if (fd == -1)
   {
      logfmt(LOG_ERROR, "[fd] open: too many open files\n");
      return -1; // EMFILE
   }

   // Create file descriptor structure
   FileDescriptor *file = (FileDescriptor *)kmalloc(sizeof(FileDescriptor));
   if (!file)
   {
      logfmt(LOG_ERROR, "[fd] open: kmalloc failed\n");
      return -1; // ENOMEM
   }

   // Copy path
   strncpy(file->path, path, 255);
   file->path[255] = '\0';

   // Set initial state
   file->offset = 0;
   file->flags = flags;
   file->readable = (flags & O_WRONLY) == 0;
   file->writable = (flags & (O_WRONLY | O_RDWR)) != 0;

   // Handle O_APPEND: seek to end
   if (flags & O_APPEND) file->offset = 0xFFFFFFFFu; // Marker for "append mode"

   // Open via VFS (resolves partition internally)
   file->inode = VFS_Open(path);
   if (!file->inode)
   {
      logfmt(LOG_ERROR, "[fd] open: file not found: %s\n", path);
      free(file);
      return -1; // ENOENT
   }

   // Store in process FD table
   proc->fd_table[fd] = file;
   logfmt(LOG_INFO, "[fd] opened: fd=%d, path=%s\n", fd, path);

   return fd;
}

// Close a file descriptor
int FD_Close(void *proc_ptr, int fd)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc) return -1;

   FileDescriptor *file = FD_Get(proc, fd);
   if (!file) return -1; // EBADF (bad file descriptor)

   // Don't allow closing stdin, stdout, stderr
   if (fd < 3) return 0; // Silently succeed

   // Close in filesystem
   if (file->inode) VFS_Close((VFS_File *)file->inode);

   free(file);
   proc->fd_table[fd] = NULL;

   logfmt(LOG_INFO, "[fd] closed: fd=%d\n", fd);
   return 0;
}

// Read from file descriptor
int FD_Read(void *proc_ptr, int fd, void *buf, uint32_t count)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc || !buf || count == 0) return -1; // EINVAL

   FileDescriptor *file = FD_Get(proc, fd);
   if (!file) return -1; // EBADF

   if (!file->readable) return -1; // EACCES (permission denied)

   // Align filesystem cursor to requested offset if needed
   if (!VFS_Seek((VFS_File *)file->inode, file->offset)) return -1;

   // Read from filesystem
   uint32_t bytes_read = VFS_Read((VFS_File *)file->inode, count, buf);
   file->offset += bytes_read;

   return bytes_read;
}

// Write to file descriptor
int FD_Write(void *proc_ptr, int fd, const void *buf, uint32_t count)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc || !buf) return -1; // EINVAL

   FileDescriptor *file = FD_Get(proc, fd);
   if (!file) return -1; // EBADF

   // Handle stdout/stderr: write to TTY streams
   if (fd == 1 || fd == 2)
   {
      TTY_Device *dev = TTY_GetDevice();
      int stream = (fd == 2) ? TTY_STREAM_STDERR : TTY_STREAM_STDOUT;
      if (dev) TTY_WriteStream(dev, stream, buf, count);
      return (int)count;
   }

   if (!file->writable) return -1; // EACCES

   // Align filesystem cursor to requested offset if needed
   if (!VFS_Seek((VFS_File *)file->inode, file->offset)) return -1;

   // Write to filesystem
   uint32_t bytes_written = VFS_Write((VFS_File *)file->inode, count, buf);
   file->offset += bytes_written;

   return bytes_written;
}

// Seek within a file
int FD_Lseek(void *proc_ptr, int fd, int32_t offset, int whence)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc) return -1;

   FileDescriptor *file = FD_Get(proc, fd);
   if (!file) return -1; // EBADF

   // whence: 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END
   switch (whence)
   {
   case 0: // SEEK_SET
      file->offset = offset;
      break;
   case 1: // SEEK_CUR
      file->offset += offset;
      break;
   case 2: // SEEK_END
      // Would need FAT_GetFileSize() to implement properly
      logfmt(LOG_WARNING, "[fd] seek: SEEK_END not yet implemented\n");
      return -1;
   default:
      return -1; // EINVAL
   }

   // Keep filesystem cursor in sync
   if (!VFS_Seek((VFS_File *)file->inode, file->offset)) return -1;

   return file->offset;
}

// Close all file descriptors for a process
void FD_CloseAll(void *proc_ptr)
{
   Process *proc = (Process *)proc_ptr;

   if (!proc) return;

   for (int i = 3; i < FD_TABLE_SIZE; i++)
   {
      if (proc->fd_table[i] != NULL) FD_Close(proc, i);
   }
}
