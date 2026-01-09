// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef FD_H
#define FD_H

#include <stdbool.h>
#include <stdint.h>

#define FD_TABLE_SIZE 16
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_APPEND 0x0400
#define O_CREAT 0x0040
#define O_TRUNC 0x0200

typedef struct
{
   char path[256];
   uint32_t offset;
   bool readable;
   bool writable;
   void *inode;
   uint32_t flags;
} FileDescriptor;

// Core FD operations
int FD_Open(void *proc, const char *path, int flags);
int FD_Close(void *proc, int fd);
int FD_Read(void *proc, int fd, void *buf, uint32_t count);
int FD_Write(void *proc, int fd, const void *buf, uint32_t count);
int FD_Lseek(void *proc, int fd, int32_t offset, int whence);

// Helper functions
FileDescriptor *FD_Get(void *proc, int fd);
int FD_FindFree(void *proc);
void FD_CloseAll(void *proc);

#endif