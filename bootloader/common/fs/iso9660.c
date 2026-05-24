// SPDX-License-Identifier: GPL-3.0-only
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

struct FS_File;
struct FS_Operations;

static int iso_read_sector(uint64_t iso_lba, void *buffer);
static int parse_dir_record(const uint8_t *buf, int off, uint32_t *extent_lba,
                            uint32_t *extent_size, uint8_t *flags,
                            uint8_t *name_len);
static int name_match(const char *component, int comp_len,
                      const uint8_t *rec_name, int rec_name_len);
static int label_nonzero(const uint8_t *label);
static int label_match(const uint8_t *isoLabel, const uint8_t *expected);
static int check_partition(uint8_t drive, int partLba,
                           const uint8_t *expectedLabel,
                           const uint8_t *expectedUuid);
static int lookup_component(uint64_t dir_lba, uint32_t dir_size,
                            const char *component, int comp_len,
                            uint32_t *out_lba, uint32_t *out_size,
                            uint8_t *out_flags);
static int resolve_path(const char *path, uint32_t *out_lba,
                        uint32_t *out_size);

/* ------------------------------------------------------------------ */
/*  Macros                                                             */
/* ------------------------------------------------------------------ */

#define SUCCESS 0
#define EINVAL (-22)
#define ENODEV (-19)
#define EIO (-5)
#define ENOENT (-2)
#define ENOTDIR (-20)
#define EMFILE (-24)
#define EBADF (-9)

#define SECTOR_SIZE_ISO 2048
#define SECTOR_SIZE_CHS 512

#define VOLUME_LABEL_OFFSET 0x28
#define VOLUME_UUID_OFFSET 0x32D
#define LABEL_SIZE 32
#define UUID_SIZE 16

#define PVD_LBA 16
#define PVD_ROOT_OFFSET 156
#define PVD_SECTOR_512                                                         \
   (PVD_LBA * (SECTOR_SIZE_ISO / SECTOR_SIZE_CHS))           /* = 64 */
#define PVD_SECTOR_COUNT (SECTOR_SIZE_ISO / SECTOR_SIZE_CHS) /* = 4 */

#define ISO_SIGNATURE "CD001"

#define MAX_OPEN_FILES 8

/* ------------------------------------------------------------------ */
/*  Structures                                                         */
/* ------------------------------------------------------------------ */

struct FS_File
{
   int used;
   uint64_t start_lba; /* absolute LBA in native sector size */
   uint32_t size;      /* file size in bytes                 */
   uint32_t position;  /* current read position              */
};

struct FS_Operations
{
   uint32_t FS_Initialize;
   uint32_t FS_Open;
   uint32_t FS_Read;
   uint32_t FS_Close;
};

/* ------------------------------------------------------------------ */
/*  Global / static variables                                          */
/* ------------------------------------------------------------------ */

static uint32_t s_BootDrive = 0;
static uint32_t s_PartStart = 0;

static uint64_t s_RootDirLBA = 0;
static uint32_t s_RootDirSize = 0;

static struct FS_File s_OpenFiles[MAX_OPEN_FILES];

extern int DISK_Read(uint8_t drive, uint16_t cylinder, uint8_t sector,
                     uint8_t head, uint8_t count, void *buffer);
extern int DISK_ReadLBA(uint8_t drive, uint64_t lba, uint16_t count,
                        void *buffer);
extern bool MBR_Probe(int driveId);
extern int MBR_List(int driveId, int **offset);
extern bool GPT_Probe(int driveId);
extern int GPT_List(int driveId, int **offset);

/* ------------------------------------------------------------------ */
/*  Private functions                                                  */
/* ------------------------------------------------------------------ */

static inline int mem_eq(const void *a, const void *b, int len)
{
   const uint8_t *pa = (const uint8_t *)a;
   const uint8_t *pb = (const uint8_t *)b;
   for (int i = 0; i < len; i++)
   {
      if (pa[i] != pb[i]) return 0;
   }
   return 1;
}

/* Stale-data workaround buffer (CD-ROM only). */
static uint8_t s_DiscardBuf[SECTOR_SIZE_ISO];

static int iso_read_sector(uint64_t iso_lba, void *buffer)
{
   uint8_t drive = (uint8_t)s_BootDrive;
   uint64_t lba;
   uint16_t count;

   if (drive >= 0xE0)
   {
      lba = (uint64_t)s_PartStart + iso_lba;
      count = 1;
   }
   else
   {
      lba = (uint64_t)s_PartStart + iso_lba * 4;
      count = 4;
   }

   if (drive >= 0xE0)
   {
      /* First read may return stale data (BIOS CD-ROM cache). */
      DISK_ReadLBA(drive, lba, count, buffer);
   }

   return DISK_ReadLBA(drive, lba, count, buffer);
}

static int parse_dir_record(const uint8_t *buf, int off, uint32_t *extent_lba,
                            uint32_t *extent_size, uint8_t *flags,
                            uint8_t *name_len)
{
   uint8_t len = buf[off];
   if (len == 0) return 0;

   *extent_lba = (uint32_t)buf[off + 2] | ((uint32_t)buf[off + 3] << 8) |
                 ((uint32_t)buf[off + 4] << 16) |
                 ((uint32_t)buf[off + 5] << 24);

   *extent_size = (uint32_t)buf[off + 10] | ((uint32_t)buf[off + 11] << 8) |
                  ((uint32_t)buf[off + 12] << 16) |
                  ((uint32_t)buf[off + 13] << 24);

   *flags = buf[off + 25];
   *name_len = buf[off + 32];
   return len;
}

static int name_match(const char *component, int comp_len,
                      const uint8_t *rec_name, int rec_name_len)
{
   int i;
   for (i = 0; i < comp_len; i++)
   {
      if (i >= rec_name_len) return 0;
      char c1 = component[i];
      char c2 = (char)rec_name[i];
      if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
      if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
      if (c1 != c2) return 0;
   }
   if (i == rec_name_len) return 1;
   if (rec_name[i] == ';') return 1;
   return 0;
}

static int label_nonzero(const uint8_t *label)
{
   for (int i = 0; i < LABEL_SIZE; i++)
   {
      if (label[i] != 0) return 1;
   }
   return 0;
}

static int label_match(const uint8_t *isoLabel, const uint8_t *expected)
{
   int i = 0;
   for (; i < LABEL_SIZE && expected[i] != 0; i++)
   {
      if (isoLabel[i] != expected[i]) return 0;
   }
   if (i == LABEL_SIZE) return 1;
   for (; i < LABEL_SIZE; i++)
   {
      if (isoLabel[i] != ' ') return 0;
   }
   return 1;
}

static int check_partition(uint8_t drive, int partLba,
                           const uint8_t *expectedLabel,
                           const uint8_t *expectedUuid)
{
   uint8_t buf[SECTOR_SIZE_ISO];
   uint64_t pvd_lba = 0;
   uint16_t pvd_count = 0;

   if (drive < 0x80) return 0;

   if (drive >= 0xE0)
   {
      pvd_lba = (uint64_t)(partLba + PVD_LBA);
      pvd_count = 1;
   }
   else
   {
      pvd_lba = (uint64_t)(partLba + PVD_SECTOR_512);
      pvd_count = PVD_SECTOR_COUNT;
   }

   if (DISK_ReadLBA(drive, pvd_lba, pvd_count, buf) != 0) return 0;

   if (buf[0] != 1) return 0;
   if (!mem_eq(&buf[1], ISO_SIGNATURE, 5)) return 0;

   if (expectedLabel && label_nonzero(expectedLabel))
   {
      if (label_match(&buf[VOLUME_LABEL_OFFSET], expectedLabel)) return 1;
   }

   if (expectedUuid)
   {
      int uuid_nonzero = 0;
      for (int i = 0; i < UUID_SIZE; i++)
      {
         if (expectedUuid[i] != 0)
         {
            uuid_nonzero = 1;
            break;
         }
      }
      if (uuid_nonzero &&
          mem_eq(&buf[VOLUME_UUID_OFFSET], expectedUuid, UUID_SIZE))
         return 1;
   }

   return 0;
}

static int lookup_component(uint64_t dir_lba, uint32_t dir_size,
                            const char *component, int comp_len,
                            uint32_t *out_lba, uint32_t *out_size,
                            uint8_t *out_flags)
{
   uint8_t buf[SECTOR_SIZE_ISO];
   uint32_t bytes_read = 0;

   while (bytes_read < dir_size)
   {
      uint64_t sector_idx = bytes_read / SECTOR_SIZE_ISO;

      if (iso_read_sector(dir_lba + sector_idx, buf) != 0) return EIO;

      int off = 0;
      while (off < SECTOR_SIZE_ISO)
      {
         uint32_t extent_lba, extent_size;
         uint8_t flags, name_len;

         int rec_len = parse_dir_record(buf, off, &extent_lba, &extent_size,
                                        &flags, &name_len);
         if (rec_len == 0) break;

         if (name_len == 1 && (buf[off + 33] == 0 || buf[off + 33] == 1))
         {
            off += rec_len;
            continue;
         }

         if (comp_len == name_len ||
             (name_len > comp_len && buf[off + 33 + comp_len] == ';'))
         {
            if (name_match(component, comp_len, &buf[off + 33], name_len))
            {
               *out_lba = extent_lba;
               *out_size = extent_size;
               *out_flags = flags;
               return 0;
            }
         }
         off += rec_len;
      }
      bytes_read += SECTOR_SIZE_ISO;
   }
   return ENOENT;
}

static int resolve_path(const char *path, uint32_t *out_lba, uint32_t *out_size)
{
   uint64_t dir_lba = s_RootDirLBA;
   uint32_t dir_size = s_RootDirSize;
   uint8_t dir_flags = 2;

   if (*path == '/') path++;

   if (*path == '\0')
   {
      *out_lba = (uint32_t)dir_lba;
      *out_size = dir_size;
      return 0;
   }

   while (*path != '\0')
   {
      if (!(dir_flags & 2)) return ENOTDIR;

      const char *start = path;
      while (*path != '/' && *path != '\0') path++;
      int comp_len = (int)(path - start);

      uint32_t child_lba, child_size;
      uint8_t child_flags;
      int rc = lookup_component(dir_lba, dir_size, start, comp_len, &child_lba,
                                &child_size, &child_flags);

      if (rc != 0) return rc;

      dir_lba = child_lba;
      dir_size = child_size;
      dir_flags = child_flags;

      while (*path == '/') path++;
   }

   *out_lba = (uint32_t)dir_lba;
   *out_size = dir_size;
   return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int FS_Initialize(const uint8_t *biosDriveList, uint32_t biosDriveListCount,
                  const uint8_t *partitionUuid, const uint8_t *partitionLabel)
{
   uint8_t buf[SECTOR_SIZE_ISO];

   if (!biosDriveList || biosDriveListCount == 0) return EINVAL;

   {
      int found = 0;
      for (uint32_t i = 0; i < biosDriveListCount && !found; i++)
      {
         uint8_t drive = biosDriveList[i];
         int *offsets = NULL;
         int count = -1;

         if (GPT_Probe(drive))
            count = GPT_List(drive, &offsets);
         else if (MBR_Probe(drive))
            count = MBR_List(drive, &offsets);

         if (count <= 0)
         {
            if (check_partition(drive, 0, partitionLabel, partitionUuid) == 1)
            {
               s_BootDrive = drive;
               s_PartStart = 0;
               found = 1;
            }
            continue;
         }

         for (int j = 0; j < count && !found; j++)
         {
            if (check_partition(drive, offsets[j], partitionLabel,
                                partitionUuid) == 1)
            {
               s_BootDrive = drive;
               s_PartStart = offsets[j];
               found = 1;
            }
         }
      }
      if (!found) return ENODEV;
   }

   if (iso_read_sector(PVD_LBA, buf) != 0) return EIO;
   if (buf[0] != 1) return EINVAL;
   if (!mem_eq(&buf[1], ISO_SIGNATURE, 5)) return EINVAL;

   {
      uint32_t root_lba, root_size;
      uint8_t root_flags, root_name_len;
      int rl = parse_dir_record(buf, PVD_ROOT_OFFSET, &root_lba, &root_size,
                                &root_flags, &root_name_len);
      if (rl == 0) return EINVAL;
      if (!(root_flags & 2)) return EINVAL;

      s_RootDirLBA = root_lba;
      s_RootDirSize = root_size;
   }

   return SUCCESS;
}

int FS_Open(const char *path)
{
   uint32_t file_lba, file_size;

   if (!path || *path == '\0') return EINVAL;

   int rc = resolve_path(path, &file_lba, &file_size);
   if (rc != 0) return rc;

   int fd;
   for (fd = 0; fd < MAX_OPEN_FILES; fd++)
   {
      if (!s_OpenFiles[fd].used) break;
   }
   if (fd == MAX_OPEN_FILES) return EMFILE;

   uint8_t drive = (uint8_t)s_BootDrive;
   uint64_t abs_lba;
   if (drive >= 0xE0)
      abs_lba = (uint64_t)s_PartStart + (uint64_t)file_lba;
   else
      abs_lba = (uint64_t)s_PartStart + (uint64_t)file_lba * 4;

   s_OpenFiles[fd].used = 1;
   s_OpenFiles[fd].start_lba = abs_lba;
   s_OpenFiles[fd].size = file_size;
   s_OpenFiles[fd].position = 0;

   return fd;
}

int FS_Read(int fd, void *buffer, int count)
{
   if (fd < 0 || fd >= MAX_OPEN_FILES || !s_OpenFiles[fd].used) return EBADF;

   struct FS_File *f = &s_OpenFiles[fd];
   uint8_t *buf = (uint8_t *)buffer;

   if (f->position >= f->size) return 0;
   if (count <= 0) return 0;

   uint32_t remaining = f->size - f->position;
   if ((uint32_t)count > remaining) count = (int)remaining;

   uint8_t drive = (uint8_t)s_BootDrive;
   uint32_t phys_ss = (drive >= 0xE0) ? SECTOR_SIZE_ISO : SECTOR_SIZE_CHS;
   uint32_t bytes_done = 0;

   while (bytes_done < (uint32_t)count)
   {
      uint32_t file_off = f->position + bytes_done;
      uint64_t phys_sector = f->start_lba + file_off / phys_ss;
      uint32_t phys_off = file_off % phys_ss;
      uint32_t chunk = phys_ss - phys_off;

      if (chunk > (uint32_t)count - bytes_done)
         chunk = (uint32_t)count - bytes_done;

      uint8_t tmp[SECTOR_SIZE_ISO];
      if (DISK_ReadLBA(drive, phys_sector, 1, tmp) != 0)
         return (bytes_done > 0) ? (int)bytes_done : EIO;

      for (uint32_t i = 0; i < chunk; i++)
         buf[bytes_done + i] = tmp[phys_off + i];

      bytes_done += chunk;
   }

   f->position += bytes_done;
   return (int)bytes_done;
}

int FS_Close(int fd)
{
   if (fd < 0 || fd >= MAX_OPEN_FILES || !s_OpenFiles[fd].used) return EBADF;

   s_OpenFiles[fd].used = 0;
   return SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Exported operations table (COREFS builds only)                     */
/* ------------------------------------------------------------------ */

#ifdef COREFS

static const struct FS_Operations fs_exports
    __attribute__((section(".exports"), used)) = {
        .FS_Initialize = (uint32_t)FS_Initialize,
        .FS_Open = (uint32_t)FS_Open,
        .FS_Read = (uint32_t)FS_Read,
        .FS_Close = (uint32_t)FS_Close,
};

#endif /* COREFS */
