// SPDX-License-Identifier: GPL-3.0-only

/*
This is a local header file, and it is not allowed to directly include
this file, so for external modules, include fs/fs.h instead.
To interact with the filesystem, use the VFS interface defined in fs/fs.h.
*/

#ifndef FAT_H
#define FAT_H
#include <stdbool.h>
#include <stdint.h>
#include <valkyrie/fs.h>

typedef struct
{
   uint8_t Name[11];
   uint8_t Attributes;
   uint8_t _Reserved;
   uint8_t CreatedTimeTenths;
   uint16_t CreatedTime;
   uint16_t CreatedDate;
   uint16_t AccessedDate;
   uint16_t FirstClusterHigh;
   uint16_t ModifiedTime;
   uint16_t ModifiedDate;
   uint16_t FirstClusterLow;
   uint32_t Size;
} __attribute__((packed)) FAT_DirectoryEntry;

typedef struct
{
   int Handle;
   bool IsDirectory;
   uint32_t Position;
   uint32_t Size;
   uint8_t Name[11]; // FAT name (11 bytes, space-padded)
} FAT_File;

enum FAT_Attributes
{
   FAT_ATTRIBUTE_READ_ONLY = 0x01,
   FAT_ATTRIBUTE_HIDDEN = 0x02,
   FAT_ATTRIBUTE_SYSTEM = 0x04,
   FAT_ATTRIBUTE_VOLUME_ID = 0x08,
   FAT_ATTRIBUTE_DIRECTORY = 0x10,
   FAT_ATTRIBUTE_ARCHIVE = 0x20,
   FAT_ATTRIBUTE_LFN = FAT_ATTRIBUTE_READ_ONLY | FAT_ATTRIBUTE_HIDDEN |
                       FAT_ATTRIBUTE_SYSTEM | FAT_ATTRIBUTE_VOLUME_ID
};

bool FAT_Initialize(Partition *disk);
FAT_File *FAT_Open(Partition *disk, const char *path);
uint32_t FAT_Read(Partition *disk, FAT_File *file, uint32_t byteCount,
                  void *dataOut);
bool FAT_ReadEntry(Partition *disk, FAT_File *file,
                   FAT_DirectoryEntry *dirEntry);
void FAT_Close(FAT_File *file);

// Seek to a specific byte position in an opened FAT file. Returns true on
// success. After seeking, the internal sector buffer will contain the sector
// covering the requested position so subsequent FAT_Read calls read from the
// requested offset.
bool FAT_Seek(Partition *disk, FAT_File *file, uint32_t position);

// Write a directory entry to a directory at the current file position.
// File must be opened as a directory. Advances file position to next entry.
bool FAT_WriteEntry(Partition *disk, FAT_File *file,
                    const FAT_DirectoryEntry *dirEntry);

// Write data to an opened file. File position is advanced by bytes written.
// File size is updated if write extends past end. Returns bytes written.
uint32_t FAT_Write(Partition *disk, FAT_File *file, uint32_t byteCount,
                   const void *dataIn);

// Truncate (shrink to 0 bytes) an opened file and free all its clusters.
// File position and size are reset to 0. Returns true on success.
bool FAT_Truncate(Partition *disk, FAT_File *file);

// Update the file's directory entry with current size and cluster info.
// Must be called after writes to persist metadata to disk.
bool FAT_UpdateEntry(Partition *disk, FAT_File *file);

// Create a new file with the given name in the root directory.
// Returns a file handle opened for writing. Returns NULL on failure.
FAT_File *FAT_Create(Partition *disk, const char *name);

// Delete a file by name from the root directory.
// Frees all clusters and marks the directory entry as deleted (0xE5).
// Returns true on success.
bool FAT_Delete(Partition *disk, const char *name);

/* Invalidate FAT cache and reset file handles */
void FAT_InvalidateCache(void);

/* VFS Integration */
struct VFS_Operations;
typedef struct VFS_Operations VFS_Operations;

/* Get VFS operations structure for FAT filesystem */
const VFS_Operations *FAT_GetVFSOperations(void);

#endif