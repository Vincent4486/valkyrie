// SPDX-License-Identifier: GPL-3.0-only
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUCCESS 0
#define EINVAL (-22)
#define ENODEV (-19)

#define SECTOR_SIZE_ISO 2048
#define SECTOR_SIZE_CHS 512

#define VOLUME_LABEL_OFFSET 0x28
#define VOLUME_UUID_OFFSET 0x32D
#define LABEL_SIZE 32
#define UUID_SIZE 16

#define PVD_LBA 16
#define PVD_SECTOR_512                                                         \
   (PVD_LBA * (SECTOR_SIZE_ISO / SECTOR_SIZE_CHS))           /* = 64 */
#define PVD_SECTOR_COUNT (SECTOR_SIZE_ISO / SECTOR_SIZE_CHS) /* = 4 */

#define ISO_SIGNATURE "CD001"

#define TRY_MATCH(drive, partLba)                                              \
   do                                                                          \
   {                                                                           \
      int r =                                                                  \
          check_partition((drive), (partLba), partitionLabel, partitionUuid);  \
      if (r == 1)                                                              \
      {                                                                        \
         s_BootDrive = (drive);                                                \
         s_PartStart = (partLba);                                              \
         return SUCCESS;                                                       \
      }                                                                        \
   } while (0)

int s_BootDrive = 0;
int s_PartStart = 0;

struct fs_operations
{
   uint32_t FS_Initialize;
   uint32_t FS_Open;
   uint32_t FS_Read;
   uint32_t FS_Close;
};

extern int DISK_Read(uint8_t drive, uint16_t cylinder, uint8_t sector,
                     uint8_t head, uint8_t count, void *buffer);
extern int DISK_ReadLBA(uint8_t drive, uint64_t lba, uint16_t count,
                        void *buffer);
extern bool MBR_Probe(int driveId);
extern int MBR_List(int driveId, int **offset);
extern bool GPT_Probe(int driveId);
extern int GPT_List(int driveId, int **offset);

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

static int uuid_match(const uint8_t *isoUuid, const uint8_t *expected)
{
   /* Compare 16-byte timestamp, ignore timezone byte. */
   return mem_eq(isoUuid, expected, UUID_SIZE);
}

static int check_partition(uint8_t drive, int partLba,
                           const uint8_t *expectedLabel,
                           const uint8_t *expectedUuid)
{
   uint8_t buf[SECTOR_SIZE_ISO];
   uint64_t pvd_lba = 0;
   uint16_t pvd_count = 0;

   /* Skip floppy drives (0x00-0x7F). */
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

   if (buf[0] != 1) return 0; /* volume descriptor type = PVD */
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
      if (uuid_nonzero && uuid_match(&buf[VOLUME_UUID_OFFSET], expectedUuid))
         return 1;
   }

   return 0;
}

int FS_Initialize(const uint8_t *biosDriveList, uint32_t biosDriveListCount,
                  const uint8_t *partitionUuid, const uint8_t *partitionLabel)
{
   if (!biosDriveList || biosDriveListCount == 0) return EINVAL;

   for (uint32_t i = 0; i < biosDriveListCount; i++)
   {
      uint8_t drive = biosDriveList[i];
      int *offsets = NULL;
      int count = -1;

      if (GPT_Probe(drive))
      {
         count = GPT_List(drive, &offsets);
      }
      else if (MBR_Probe(drive))
      {
         count = MBR_List(drive, &offsets);
      }
      else
      {
         TRY_MATCH(drive, 0);
         continue;
      }

      if (count > 0)
      {
         for (int j = 0; j < count; j++)
         {
            TRY_MATCH(drive, offsets[j]);
         }
      }
   }

   return ENODEV;
}

int FS_Open(void) { return ENODEV; }
int FS_Read(void) { return EINVAL; }
int FS_Close(void) { return SUCCESS; }

#ifdef COREFS

static const struct fs_operations fs_exports
    __attribute__((section(".exports"), used)) = {
        .FS_Initialize = (uint32_t)FS_Initialize,
        .FS_Open = (uint32_t)FS_Open,
        .FS_Read = (uint32_t)FS_Read,
        .FS_Close = (uint32_t)FS_Close,
};

#endif /* COREFS */
