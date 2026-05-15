// SPDX-License-Identifier: GPL-3.0-only

// #include <"video/logo_gen.h">
#include "video/video.h"
#include <stdint.h>

/* Multiboot2 tag types */
#define MBI_TAG_END 0
#define MBI_TAG_MMAP 6
#define MBI_TAG_FRAMEBUFFER 8

int g_PrimaryOutputSystem = 0;
int preferedOutput = OUTPUT_VGATEXT;

struct fs_operations
{
   uint32_t FS_Initialize;
   uint32_t FS_Open;
   uint32_t FS_Read;
   uint32_t FS_Close;
};

struct mbi_tag_framebuffer
{
   uint32_t type;
   uint32_t size;
   uint64_t framebuffer_addr;
   uint32_t framebuffer_pitch;
   uint32_t framebuffer_width;
   uint32_t framebuffer_height;
   uint8_t framebuffer_bpp;
   uint8_t framebuffer_type;
   uint16_t reserved;
   uint8_t red_field_position;
   uint8_t red_mask_size;
   uint8_t green_field_position;
   uint8_t green_mask_size;
   uint8_t blue_field_position;
   uint8_t blue_mask_size;
   uint8_t rgb_reserved[2];
};

typedef struct
{
   uint32_t mbiAddr;
   uint32_t corefsAddr;
   uint32_t availableOutputs;
   uint32_t bootDrive;
   uint32_t biosDriveListAddr;
   uint32_t biosDriveListCount;
   uint32_t corefsPartitionUuidAddr;
   uint32_t corefsPartitionLabelAddr;
} BootParams;

static void init_framebuffer_info(uint8_t *ptr)
{
   for (;;)
   {
      uint32_t type = *(uint32_t *)ptr;
      uint32_t size = *(uint32_t *)(ptr + 4);

      if (type == MBI_TAG_END) break;

      if (type == MBI_TAG_FRAMEBUFFER &&
          size >= sizeof(struct mbi_tag_framebuffer))
      {
         const struct mbi_tag_framebuffer *tag =
             (const struct mbi_tag_framebuffer *)ptr;
         if (tag->framebuffer_type == 1)
         {
            VBE_Info info;
            info.framebuffer_addr = tag->framebuffer_addr;
            info.pitch = tag->framebuffer_pitch;
            info.width = tag->framebuffer_width;
            info.height = tag->framebuffer_height;
            info.bpp = tag->framebuffer_bpp;
            info.red_field_position = tag->red_field_position;
            info.red_mask_size = tag->red_mask_size;
            info.green_field_position = tag->green_field_position;
            info.green_mask_size = tag->green_mask_size;
            info.blue_field_position = tag->blue_field_position;
            info.blue_mask_size = tag->blue_mask_size;
            VBE_SetInfo(&info);
         }
      }

      /* Advance to next tag (8-byte aligned) */
      ptr += size;
      ptr = (uint8_t *)(((uintptr_t)ptr + 7) & ~(uintptr_t)7);
   }
}

void print_memory_map(uint8_t *ptr)
{
   puts("Memory Map:\n");
   for (;;)
   {
      uint32_t type = *(uint32_t *)ptr;
      uint32_t size = *(uint32_t *)(ptr + 4);

      if (type == MBI_TAG_END) break;

      if (type == MBI_TAG_MMAP)
      {
         uint32_t entry_size = *(uint32_t *)(ptr + 8);
         /* uint32_t entry_version = *(uint32_t *)(ptr + 12); */
         uint8_t *entry = ptr + 16;
         uint32_t total_size = size - 16;
         uint32_t count = total_size / entry_size;
         uint32_t i;

         for (i = 0; i < count; i++)
         {
            uint64_t base = *(uint64_t *)entry;
            uint64_t len = *(uint64_t *)(entry + 8);
            uint32_t type2 = *(uint32_t *)(entry + 16);

            puts("  base=");
            putx(base);
            putc('\n');

            puts("  len =");
            putx(len);
            putc('\n');

            puts("  type=");
            puti((int)type2);
            putc('\n');

            puts("  --\n");

            entry += entry_size;
         }
      }

      /* Advance to next tag (8-byte aligned) */
      ptr += size;
      ptr = (uint8_t *)(((uintptr_t)ptr + 7) & ~(uintptr_t)7);
   }
   putc('\n');
}

/* Print which output systems are reported as available. */
void print_available_outputs(uint8_t availableOutputs)
{
   puts("Available outputs:\n");

   if (availableOutputs & (1 << OUTPUT_VBE)) puts("  VBE\n");
   if (availableOutputs & (1 << OUTPUT_VGA)) puts("  VGA graphics\n");
   if (availableOutputs & (1 << OUTPUT_VGATEXT)) puts("  VGA text\n");
   if (availableOutputs & (1 << OUTPUT_SERIAL)) puts("  Serial (COM1)\n");

   putc('\n');
}

void print_boot_drive_number(int bootDrive)
{
   char *driveType;
   if (bootDrive == 0xe0)
      driveType = "CD/DVD";
   else if (bootDrive < 0x80)
      driveType = "Floppy Disk";
   else
      driveType = "Hard Disk";

   puts("Boot drive information:\n");

   puts("  Boot Drive Number: ");
   puts("0x");
   putx(bootDrive);
   puts(".\n");

   puts("  Booted from a ");
   puts(driveType);
   puts(".\n\n");
}

static void print_bios_drive_list(const uint8_t *driveList, uint32_t driveCount)
{
   uint32_t i;

   puts("Detected BIOS drives:\n");

   if (!driveList || driveCount == 0)
   {
      puts("  (none)\n\n");
      return;
   }

   for (i = 0; i < driveCount; i++)
   {
      puts("  0x");
      putx(driveList[i]);
      putc('\n');
   }

   putc('\n');
}

void print_corefs_memory_address(uint32_t address)
{
   puts("Corefs Module location: ");
   putx(address);
   puts(".\n\n");
}

static void print_stage3_fs_location(const BootParams *bootParams)
{
   puts("Partition label: \"");
   puts((const char *)(uintptr_t)bootParams->corefsPartitionLabelAddr);
   puts("\".\n");

   puts("Partition UUID: ");
   {
      const uint8_t *uuid =
          (const uint8_t *)bootParams->corefsPartitionUuidAddr;
      for (int i = 0; i < 16; i++)
      {
         if (i == 4 || i == 6 || i == 8 || i == 10) putc('-');
         putx(uuid[i]);
      }
   }
   puts(".\n\n");
}

int main(const BootParams *bootParams)
{
   uint8_t *ptr = (uint8_t *)(uintptr_t)bootParams->mbiAddr + 8;
   uint8_t availableOutputs = (uint8_t)bootParams->availableOutputs;
   uint8_t bootDrive = (uint8_t)bootParams->bootDrive;
   const uint8_t *biosDriveList =
       (const uint8_t *)(uintptr_t)bootParams->biosDriveListAddr;
   uint32_t biosDriveListCount = bootParams->biosDriveListCount;

   /* Determine preferred output — highest available wins.
      Priority (ascending): serial → VGA text → VGA graphics → VBE. */
   init_framebuffer_info(ptr);

   preferedOutput = OUTPUT_SERIAL; /* fallback  */
   if (availableOutputs & (1 << OUTPUT_VGATEXT))
      preferedOutput = OUTPUT_VGATEXT;
   if (availableOutputs & (1 << OUTPUT_VGA)) preferedOutput = OUTPUT_VGA;
   if ((availableOutputs & (1 << OUTPUT_VBE)) && VBE_HasInfo())
      preferedOutput = OUTPUT_VBE;

   /* Initialise ONLY the chosen output system.
      VGA/VBE switch the hardware to graphics mode, which destroys text-mode
      output — so they must NOT be initialised unless they are the final pick.
    */
   switch (preferedOutput)
   {
   case OUTPUT_SERIAL:
      Serial_Initialize();
      break;
   case OUTPUT_VGATEXT:
      VGATEXT_Initialize();
      break;
   case OUTPUT_VGA:
      VGA_Initialize();
      break;
   case OUTPUT_VBE:
      VBE_Initialize();
      break;
   }

   g_PrimaryOutputSystem = availableOutputs;

   print_available_outputs(availableOutputs);
   print_memory_map(ptr);
   print_boot_drive_number(bootDrive);
   print_bios_drive_list(biosDriveList, biosDriveListCount);
   print_corefs_memory_address(bootParams->corefsAddr);
   print_stage3_fs_location(bootParams);

   /* Call the corefs driver's initialization */
   {
      puts("Entering filesystem setup.\n");
      struct fs_operations *fs_ops =
          (struct fs_operations *)bootParams->corefsAddr;
      const uint8_t *partitionUuid =
          (const uint8_t *)(uintptr_t)bootParams->corefsPartitionUuidAddr;
      const uint8_t *partitionLabel =
          (const uint8_t *)(uintptr_t)bootParams->corefsPartitionLabelAddr;

      typedef int (*fs_init_fn)(const uint8_t *, const uint8_t *,
                                const uint8_t *);
      fs_init_fn FS_Initialize = (fs_init_fn)fs_ops->FS_Initialize;
      int rc = FS_Initialize(biosDriveList, partitionUuid, partitionLabel);
      if (rc < 0)
      {
         puts("  FS_Initialize failed: ");
         puti(rc);
         puts(".\n");
      }
      else
      {
         puts("  FS initialized successful.");
      }
   }

   return 0;
}
