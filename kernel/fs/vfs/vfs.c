// SPDX-License-Identifier: AGPL-3.0-or-later

#include "vfs.h"

#include <fs/fat/fat.h>
#include <fs/fs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdbool.h>
#include <stdint.h>

/* Get VFS operations for a filesystem type */
static const VFS_Operations* get_fs_operations(FilesystemType type)
{
    switch (type) {
        case FAT12:
        case FAT16:
        case FAT32:
            return FAT_GetVFSOperations();
        default:
            return NULL;
    }
}

#define VFS_MAX_MOUNTS 8
#define VFS_MAX_PATH 256

typedef struct
{
   char mount_point[VFS_MAX_PATH];
   Partition *partition;
} VFS_MountEntry;

static VFS_MountEntry g_mounts[VFS_MAX_MOUNTS];
static uint8_t g_mount_count = 0;

void VFS_Init(void)
{
   g_mount_count = 0;
   memset(g_mounts, 0, sizeof(g_mounts));
}

static bool vfs_normalize_mount(const char *location, char *normalized,
                                size_t normalized_size)
{
   if (!location || normalized_size == 0) return false;

   size_t len = strlen(location);
   if (len == 0) return false;

   if (location[0] != '/')
   {
      printf("[VFS] Mount point '%s' must start with '/'\n", location);
      return false;
   }

   /* Strip trailing slashes except for root */
   while (len > 1 && location[len - 1] == '/') len--;

   if (len >= normalized_size) len = normalized_size - 1;

   memcpy(normalized, location, len);
   normalized[len] = '\0';
   return true;
}

static const VFS_MountEntry *vfs_match_mount(const char *path,
                                             size_t *prefix_len_out)
{
   if (!path || g_mount_count == 0) return NULL;

   const VFS_MountEntry *best = NULL;
   size_t best_len = 0;

   for (uint8_t i = 0; i < g_mount_count; i++)
   {
      const char *mount = g_mounts[i].mount_point;
      size_t mount_len = strlen(mount);

      if (strncmp(path, mount, mount_len) != 0) continue;

      /* Require boundary match (exact or next char is '/') */
      if (path[mount_len] != '\0' && path[mount_len] != '/' &&
          mount[mount_len - 1] != '/')
         continue;

      if (mount_len > best_len)
      {
         best = &g_mounts[i];
         best_len = mount_len;
      }
   }

   if (prefix_len_out) *prefix_len_out = best_len;
   return best;
}

static bool vfs_resolve_path(const char *path, Partition **part_out,
                             char *relative_out, size_t relative_size)
{
   if (!path || !part_out || !relative_out || relative_size == 0) return false;

   if (*path == '\0') path = "/";

   size_t prefix_len = 0;
   const VFS_MountEntry *mount = vfs_match_mount(path, &prefix_len);
   if (!mount) return false;

   const char *tail = path + prefix_len;
   if (*tail == '\0')
   {
      strncpy(relative_out, "/", relative_size);
   }
   else if (*tail != '/')
   {
      snprintf(relative_out, relative_size, "/%s", tail);
   }
   else
   {
      strncpy(relative_out, tail, relative_size - 1);
      relative_out[relative_size - 1] = '\0';
   }

   *part_out = mount->partition;
   return true;
}

int FS_Mount(Partition *volume, const char *location)
{
   if (!volume || !volume->disk)
   {
      printf("[VFS] Invalid volume for mount\n");
      return -1;
   }

   if (!volume->fs)
   {
      printf("[VFS] No filesystem initialized on this volume\n");
      return -1;
   }

   if (g_mount_count >= VFS_MAX_MOUNTS)
   {
      printf("[VFS] Mount table full\n");
      return -1;
   }

   char normalized[VFS_MAX_PATH];
   if (!vfs_normalize_mount(location, normalized, sizeof(normalized)))
   {
      printf("[VFS] Invalid mount location '%s'\n", location ? location : "");
      return -1;
   }

   /* Avoid duplicate mounts on the same path */
   for (uint8_t i = 0; i < g_mount_count; i++)
   {
      if (strncmp(g_mounts[i].mount_point, normalized, sizeof(normalized)) == 0)
      {
         printf("[VFS] Mount point '%s' already in use\n", normalized);
         return -1;
      }
   }

   /* Set the VFS operations based on filesystem type */
   if (!volume->fs->ops)
   {
      volume->fs->ops = get_fs_operations(volume->fs->type);
      if (!volume->fs->ops)
      {
         printf("[VFS] No operations available for filesystem type %d\n", volume->fs->type);
         return -1;
      }
   }

   strncpy(g_mounts[g_mount_count].mount_point, normalized,
           sizeof(g_mounts[g_mount_count].mount_point) - 1);
   g_mounts[g_mount_count]
       .mount_point[sizeof(g_mounts[g_mount_count].mount_point) - 1] = '\0';
   g_mounts[g_mount_count].partition = volume;
   g_mount_count++;

   volume->fs->mounted = 1;
   printf("[VFS] Mounted %s at %s\n", volume->disk->brand, normalized);
   return 0;
}

int FS_Umount(Partition *volume)
{
   if (!volume || !volume->fs) return -1;

   for (uint8_t i = 0; i < g_mount_count; i++)
   {
      if (g_mounts[i].partition == volume)
      {
         /* Compact the table by moving the last entry down */
         g_mounts[i] = g_mounts[g_mount_count - 1];
         g_mount_count--;
         volume->fs->mounted = 0;
         return 0;
      }
   }

   return -1;
}

VFS_File *VFS_Open(const char *path)
{
   Partition *part = NULL;
   char relative[VFS_MAX_PATH];

   if (!vfs_resolve_path(path, &part, relative, sizeof(relative)))
   {
      printf("[VFS] No mount found for path '%s'\n", path ? path : "");
      return NULL;
   }

   if (!part || !part->fs || !part->fs->ops)
   {
      printf("[VFS] Missing filesystem or operations for resolved partition\n");
      printf("[VFS DEBUG] part=%p, part->fs=%p, part->fs->ops=%p\n",
             (void*)part,
             (void*)(part ? part->fs : NULL),
             (void*)(part && part->fs ? part->fs->ops : NULL));
      if (part && part->disk)
         printf("[VFS DEBUG] partition->disk=%p brand=%s\n",
                (void*)part->disk,
                part->disk->brand ? part->disk->brand : "NULL");
      return NULL;
   }

   if (!part->fs->ops->open)
   {
      printf("[VFS] Filesystem does not support open operation\n");
      return NULL;
   }

   return part->fs->ops->open(part, relative);
}

bool VFS_Delete(const char *path)
{
   Partition *part = NULL;
   char relative[VFS_MAX_PATH];

   if (!vfs_resolve_path(path, &part, relative, sizeof(relative)))
   {
      printf("[VFS] No mount found for path '%s'\n", path ? path : "");
      return false;
   }

   if (!part || !part->fs || !part->fs->ops || !part->fs->ops->delete)
   {
      printf("[VFS] Filesystem does not support delete operation\n");
      printf("[VFS DEBUG] delete check: part=%p, part->fs=%p, part->fs->ops=%p, delete_fn=%p\n",
             (void*)part,
             (void*)(part ? part->fs : NULL),
             (void*)(part && part->fs ? part->fs->ops : NULL),
             (void*)(part && part->fs && part->fs->ops ? part->fs->ops->delete : NULL));
      return false;
   }

   return part->fs->ops->delete(part, relative);
}

uint32_t VFS_Read(VFS_File *file, uint32_t byteCount, void *dataOut)
{
   if (!file || !dataOut || byteCount == 0) return 0;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops || !file->partition->fs->ops->read)
      return 0;

   return file->partition->fs->ops->read(file->partition, file->fs_file, byteCount, dataOut);
}

uint32_t VFS_Write(VFS_File *file, uint32_t byteCount, const void *dataIn)
{
   if (!file || !dataIn || byteCount == 0) return 0;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops || !file->partition->fs->ops->write)
      return 0;

   return file->partition->fs->ops->write(file->partition, file->fs_file, byteCount, dataIn);
}

bool VFS_Seek(VFS_File *file, uint32_t position)
{
   if (!file) return false;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops || !file->partition->fs->ops->seek)
      return false;

   return file->partition->fs->ops->seek(file->partition, file->fs_file, position);
}

void VFS_Close(VFS_File *file)
{
   if (!file) return;

   if (file->partition && file->partition->fs && file->partition->fs->ops && 
       file->partition->fs->ops->close && file->fs_file)
   {
      file->partition->fs->ops->close(file->fs_file);
   }

   free(file);
}

uint32_t VFS_GetSize(VFS_File *file)
{
   if (!file) return 0;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops || !file->partition->fs->ops->get_size)
      return file->size; /* Fallback to cached size */

   return file->partition->fs->ops->get_size(file->fs_file);
}

void VFS_SelfTest(void)
{
   const char *test_path = "/test/vfs.txt";
   const uint32_t test_size = 4096; // 4 KB
   uint32_t alphabet_len = 26;
   
   char *test_data = (char *)kmalloc(sizeof(char) * test_size);
   VFS_File *test_file = NULL;

   if (test_data == NULL)
   {
      printf("[VFS] Failed to allocate test buffer\n");
      goto cleanup;
   }
   memset(test_data, 'A', sizeof(test_data) - 1);
   // test_data[test_size] = '\0';

   test_file = VFS_Open(test_path);
   if (!test_file)
   {
      printf("[VFS] Failed to open/create file\n");
      goto cleanup;
   }

   uint32_t bytes_written = VFS_Write(test_file, test_size, test_data);

   if (bytes_written != test_size)
   {
      printf("[VFS] SelfTest=FAILED\n");
   }
   else
   {
      printf("[VFS] SelfTest=PASS\n");
   }

   VFS_Close(test_file);
   test_file = NULL;

cleanup:
   if (test_file)
   {
      VFS_Close(test_file);
   }

   if (test_data)
   {
      free(test_data);
   }
}
