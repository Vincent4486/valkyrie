// SPDX-License-Identifier: GPL-3.0-only
#include <stddef.h>
#include <stdint.h>

/* Error codes (negative errno convention). */
#define SUCCESS 0
#define EINVAL (-22)
#define ENODEV (-19)

struct fs_operations
{
   uint32_t FS_Initialize;
   uint32_t FS_Open;
   uint32_t FS_Read;
   uint32_t FS_Close;
};

extern int DISK_Read(uint8_t drive, uint16_t cylinder, uint8_t sector,
                     uint8_t head, uint8_t count, void *buffer);

int FS_Initialize(const uint8_t *biosDriveList, const uint8_t *partitionUuid,
                  const uint8_t *partitionLabel)
{
   (void)biosDriveList;
   (void)partitionUuid;
   (void)partitionLabel;

   return SUCCESS;
}
int FS_Open(void) { return -ENODEV; }
int FS_Read(void) { return -EINVAL; }
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
