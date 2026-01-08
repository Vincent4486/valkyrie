// SPDX-License-Identifier: AGPL-3.0-or-later

#include <cpu/cpu.h>
#include <cpu/process.h>
#include <drivers/ata/ata.h>
#include <fs/disk/disk.h>
#include <fs/disk/partition.h>
#include <fs/fat/fat.h>
#include <fs/fs.h>
#include <hal/hal.h>
#include <hal/irq.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>
#include <sys/dylib.h>
#include <sys/elf.h>
#include <sys/sys.h>
#include <valkyrie/system.h>

#include <display/startscreen.h>
#include <libmath/math.h>

extern uint8_t __bss_start;
extern uint8_t __end;
extern void _init();

void __attribute__((section(".entry"))) start(uint16_t bootDrive,
                                              void *multiboot_info_ptr)
{
   // Init system
   memset(&__bss_start, 0, (&__end) - (&__bss_start));
   _init();

   memset(g_SysInfo, 0, sizeof(SYS_Info));
   g_SysInfo->boot_device = bootDrive;

   MEM_Initialize(multiboot_info_ptr);
   SYS_Initialize();
   CPU_Initialize();
   HAL_Initialize();

   if (!FS_Initialize())
   {
      printf("FS initialization failed\n");
      goto end;
   }
   FS_Mount(&g_SysInfo->volume[0], "/");

   if (!Dylib_Initialize(&g_SysInfo->volume[0]))
   {
      printf("Failed to load dynamic libraries...");
      goto end;
   }

   /* Mark system as fully initialized */
   SYS_Finalize();
   ELF_LoadProcess(&g_SysInfo->volume[0], "/usr/bin/sh", false);

   uint32_t last_uptime = 0;
   while (g_SysInfo->uptime_seconds < 1000)
   {
      /* Update uptime from tick counter */
      g_SysInfo->uptime_seconds = system_ticks / 1000;
      if (g_SysInfo->uptime_seconds != last_uptime)
      {
         printf("\rSystem up for %u seconds", g_SysInfo->uptime_seconds);
         last_uptime = g_SysInfo->uptime_seconds;
      }

      /* Idle efficiently until next interrupt: enable interrupts, HLT,
         then disable again. Matches i686 PS/2 idle usage. */
      __asm__ volatile("sti; hlt; cli");
   }
   printf("\n");

end:
   for (;;);
}
