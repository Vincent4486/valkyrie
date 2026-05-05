// SPDX-License-Identifier: GPL-3.0-only

#include <stdint.h>
#include "video/video.h"
#include "video/logo.h"

/* Multiboot2 tag types */
#define MBI_TAG_END          0
#define MBI_TAG_MMAP         6
#define MBI_TAG_FRAMEBUFFER  8

int g_PrimaryOutputSystem = 0;
int preferedOutput = OUTPUT_VGATEXT;

struct mbi_tag_framebuffer
{
   uint32_t type;
   uint32_t size;
   uint64_t framebuffer_addr;
   uint32_t framebuffer_pitch;
   uint32_t framebuffer_width;
   uint32_t framebuffer_height;
   uint8_t  framebuffer_bpp;
   uint8_t  framebuffer_type;
   uint16_t reserved;
   uint8_t  red_field_position;
   uint8_t  red_mask_size;
   uint8_t  green_field_position;
   uint8_t  green_mask_size;
   uint8_t  blue_field_position;
   uint8_t  blue_mask_size;
   uint8_t  rgb_reserved[2];
};

#define BOOT_LOGO_SCALE 5

static void draw_boot_logo(int origin_x)
{
   uint32_t palette[VALECIUM_LOGO_PALETTE_SIZE];
   uint32_t screen_w;
   uint32_t screen_h;
   uint32_t i;
   int scale;
   int origin_y;
   int x, y, sx, sy;

   if (!VBE_HasInfo() || origin_x < 0)
      return;

   for (i = 0; i < VALECIUM_LOGO_PALETTE_SIZE; i++)
   {
      uint8_t r = g_ValeciumLogo_PaletteRGB[i * 3u + 0u];
      uint8_t g = g_ValeciumLogo_PaletteRGB[i * 3u + 1u];
      uint8_t b = g_ValeciumLogo_PaletteRGB[i * 3u + 2u];
      palette[i] = VBE_PackRGB(r, g, b);
   }

   scale = BOOT_LOGO_SCALE;
   screen_w = VBE_GetWidth();
   screen_h = VBE_GetHeight();
   if (screen_w && screen_h)
   {
      int max_scale_x = (int)(screen_w / (uint32_t)VALECIUM_LOGO_W);
      int max_scale_y = (int)(screen_h / (uint32_t)VALECIUM_LOGO_H);
      int max_scale = (max_scale_x < max_scale_y) ? max_scale_x : max_scale_y;
      if (max_scale < 1)
         max_scale = 1;
      if (scale > max_scale)
         scale = max_scale;
   }

   origin_y = ((int)screen_h - (int)VALECIUM_LOGO_H * scale) / 2;
   if (origin_y < 0)
      origin_y = 0;

   for (y = 0; y < VALECIUM_LOGO_H; y++)
   {
      for (x = 0; x < VALECIUM_LOGO_W; x++)
      {
         uint32_t i = (uint32_t)y * (uint32_t)VALECIUM_LOGO_W + (uint32_t)x;
         uint8_t b = g_ValeciumLogo_Data4bpp[i >> 1];
         uint8_t idx = (i & 1u) ? (b & 0x0Fu) : (uint8_t)((b >> 4) & 0x0Fu);
         uint32_t color = palette[idx & 0x0Fu];

         for (sy = 0; sy < scale; sy++)
            for (sx = 0; sx < scale; sx++)
               VBE_PutPixel(color,
                  origin_x + x * scale + sx,
                  origin_y + y * scale + sy);
      }
   }
}

static void init_framebuffer_info(uint8_t *ptr)
{
   for (;;)
   {
      uint32_t type = *(uint32_t *)ptr;
      uint32_t size = *(uint32_t *)(ptr + 4);

      if (type == MBI_TAG_END)
         break;

      if (type == MBI_TAG_FRAMEBUFFER && size >= sizeof(struct mbi_tag_framebuffer))
      {
         const struct mbi_tag_framebuffer *tag = (const struct mbi_tag_framebuffer *)ptr;
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

      if (type == MBI_TAG_END)
         break;

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
            uint64_t base  = *(uint64_t *)entry;
            uint64_t len   = *(uint64_t *)(entry + 8);
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
}

/* Print which output systems are reported as available. */
void print_available_outputs(uint8_t availableOutputs)
{
   puts("Available outputs:\n");

   if (availableOutputs & (1 << OUTPUT_VBE))
      puts("  VBE\n");
   if (availableOutputs & (1 << OUTPUT_VGA))
      puts("  VGA graphics\n");
   if (availableOutputs & (1 << OUTPUT_VGATEXT))
      puts("  VGA text\n");
   if (availableOutputs & (1 << OUTPUT_SERIAL))
      puts("  Serial (COM1)\n");

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
   puts(".\n");
}

/* Walk the Multiboot2 Boot Information structure at @mbi_addr
 * and print the memory map entries. */
int main(uint32_t mbi_addr, uint8_t availableOutputs, uint8_t bootDrive)
{
   uint8_t *ptr = (uint8_t *)(uintptr_t)mbi_addr + 8;  /* skip total_size + reserved */

   /* Determine preferred output — highest available wins.
      Priority (ascending): serial → VGA text → VGA graphics → VBE. */
   init_framebuffer_info(ptr);

   preferedOutput = OUTPUT_SERIAL;                          /* fallback  */
   if (availableOutputs & (1 << OUTPUT_VGATEXT))
      preferedOutput = OUTPUT_VGATEXT;
   if (availableOutputs & (1 << OUTPUT_VGA))
      preferedOutput = OUTPUT_VGA;
   if ((availableOutputs & (1 << OUTPUT_VBE)) && VBE_HasInfo())
      preferedOutput = OUTPUT_VBE;

   /* Initialise ONLY the chosen output system.
      VGA/VBE switch the hardware to graphics mode, which destroys text-mode
      output — so they must NOT be initialised unless they are the final pick. */
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

   puts("Valecium Bootloader loaded.\n");

   print_available_outputs(availableOutputs);
   print_memory_map(ptr);
   print_boot_drive_number(bootDrive);
   if (preferedOutput == OUTPUT_VBE)
      draw_boot_logo(VBE_GetWidth() - 720);
   return 0;
}