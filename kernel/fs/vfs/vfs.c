// SPDX-License-Identifier: AGPL-3.0-or-later

#include "vfs.h"

#include <fs/fat/fat.h>
#include <fs/fs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/sys.h>

/* Get VFS operations for a filesystem type */
static const VFS_Operations *get_fs_operations(FilesystemType type)
{
   switch (type)
   {
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
      relative_out[0] = '/';
      relative_out[1] = '\0';
   }
   else if (*tail != '/')
   {
      /* Manually prepend '/' instead of using snprintf */
      relative_out[0] = '/';
      size_t tail_len = strlen(tail);
      if (tail_len + 1 >= relative_size)
      {
         return false;
      }
      strncpy(relative_out + 1, tail, relative_size - 1);
      relative_out[relative_size - 1] = '\0';
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

   char *normalized = kmalloc(VFS_MAX_PATH);
   if (!normalized)
   {
      printf("[VFS] Failed to allocate mount path buffer\n");
      return -1;
   }
   
   if (!vfs_normalize_mount(location, normalized, VFS_MAX_PATH))
   {
      printf("[VFS] Invalid mount location '%s'\n", location ? location : "");
      free(normalized);
      return -1;
   }

   /* Avoid duplicate mounts on the same path */
   for (uint8_t i = 0; i < g_mount_count; i++)
   {
      if (strncmp(g_mounts[i].mount_point, normalized, VFS_MAX_PATH) == 0)
      {
         printf("[VFS] Mount point '%s' already in use\n", normalized);
         free(normalized);
         return -1;
      }
   }

   /* Set the VFS operations based on filesystem type */
   if (!volume->fs->ops)
   {
      volume->fs->ops = get_fs_operations(volume->fs->type);
      if (!volume->fs->ops)
      {
         printf("[VFS] No operations available for filesystem type %d\n",
                volume->fs->type);
         free(normalized);
         return -1;
      }
   }

   /* Debug: print mount pointers so we can verify partition/fs correctness */
   printf("[VFS] Mounting partition @%p -> fs=%p ops=%p at %s\n",
          (void *)volume, (void *)volume->fs,
          (void *)(volume->fs ? volume->fs->ops : NULL), normalized);

   strncpy(g_mounts[g_mount_count].mount_point, normalized,
           sizeof(g_mounts[g_mount_count].mount_point) - 1);
   g_mounts[g_mount_count]
       .mount_point[sizeof(g_mounts[g_mount_count].mount_point) - 1] = '\0';
   g_mounts[g_mount_count].partition = volume;
   g_mount_count++;

   volume->fs->mounted = 1;
   free(normalized);
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
   char *relative = kmalloc(VFS_MAX_PATH);
   if (!relative)
   {
      return NULL;
   }

   if (!vfs_resolve_path(path, &part, relative, VFS_MAX_PATH))
   {
      free(relative);
      return NULL;
   }

   if (!part || !part->fs || !part->fs->ops || !part->fs->ops->open)
   {
      free(relative);
      return NULL;
   }

   VFS_File *result = part->fs->ops->open(part, relative);
   free(relative);
   return result;
}

bool VFS_Delete(const char *path)
{
   Partition *part = NULL;
   char *relative = kmalloc(VFS_MAX_PATH);
   if (!relative)
   {
      return false;
   }

   if (!vfs_resolve_path(path, &part, relative, VFS_MAX_PATH))
   {
      printf("[VFS] No mount found for path '%s'\n", path ? path : "");
      free(relative);
      return false;
   }

   if (!part || !part->fs || !part->fs->ops || !part->fs->ops->delete)
   {
      printf("[VFS DEBUG] delete check: part=%p, part->fs=%p, "
             "part->fs->ops=%p, delete_fn=%p\n",
             (void *)part, (void *)(part ? part->fs : NULL),
             (void *)(part && part->fs ? part->fs->ops : NULL),
             (void *)(part && part->fs && part->fs->ops ? part->fs->ops->delete
                                                        : NULL));
      free(relative);
      return false;
   }

   bool result = part->fs->ops->delete (part, relative);
   free(relative);
   return result;
}

uint32_t VFS_Read(VFS_File *file, uint32_t byteCount, void *dataOut)
{
   if (!file || !dataOut || byteCount == 0) return 0;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops ||
       !file->partition->fs->ops->read)
      return 0;

   uint32_t result = file->partition->fs->ops->read(
       file->partition, file->fs_file, byteCount, dataOut);
   return result;
}

uint32_t VFS_Write(VFS_File *file, uint32_t byteCount, const void *dataIn)
{
   if (!file || !dataIn || byteCount == 0) return 0;
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops ||
       !file->partition->fs->ops->write)
      return 0;

   return file->partition->fs->ops->write(file->partition, file->fs_file,
                                          byteCount, dataIn);
}

bool VFS_Seek(VFS_File *file, uint32_t position)
{
   if (!file)
   {
      printf("[VFS_Seek] file is NULL\n");
      return false;
   }
   if (!file->partition)
   {
      printf("[VFS_Seek] partition is NULL\n");
      return false;
   }
   if (!file->partition->fs)
   {
      printf("[VFS_Seek] fs is NULL\n");
      return false;
   }
   if (!file->partition->fs->ops)
   {
      printf("[VFS_Seek] ops is NULL\n");
      return false;
   }
   if (!file->partition->fs->ops->seek)
   {
      printf("[VFS_Seek] seek function pointer is NULL\n");
      return false;
   }

   return file->partition->fs->ops->seek(file->partition, file->fs_file,
                                         position);
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
   if (!file->partition || !file->partition->fs || !file->partition->fs->ops ||
       !file->partition->fs->ops->get_size)
      return file->size; /* Fallback to cached size */

   return file->partition->fs->ops->get_size(file->fs_file);
}

void VFS_SelfTest(void)
{
   const char *test_path = "/test/vfs.txt";
   const char *test_data_str = "hello";
   uint32_t len = strlen(test_data_str);
   VFS_File *test_file = NULL;

   test_file = VFS_Open(test_path);
   if (!test_file)
   {
      printf("[VFS] Failed to open/create file\n");
      printf("[VFS] SelfTest: done\n");
      return;
   }

   uint32_t bytes_written = VFS_Write(test_file, len, test_data_str);

   if (bytes_written != len)
   {
      printf("[VFS] SelfTest=FAILED (wrote %u/%u bytes)\n", bytes_written, len);
   }
   else
   {
      printf("[VFS] SelfTest=PASS\n");
   }

   VFS_Close(test_file);
   test_file = NULL;
}
