// SPDX-License-Identifier: GPL-3.0-only

#include <cpu/cpu.h>
#include <cpu/process.h>
#include <drivers/ata/ata.h>
#include <drivers/keyboard/keyboard.h>
#include <drivers/tty/tty.h>
#include <fs/devfs/devfs.h>
#include <fs/fd/fd.h>
#include <hal/hal.h>
#include <hal/io.h>
#include <hal/irq.h>
#include <hal/scheduler.h>
#include <hal/video.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>
#include <sys/cmdline.h>
#include <sys/dylib.h>
#include <sys/elf.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>
#include <valkyrie/system.h>

#include <libmath/math.h>

extern int Init_MountRoot(void);
extern void interact();

extern uint8_t __bss_start;
extern uint8_t __end;
extern void _init();

void hold(int sec)
{
   uint32_t last_uptime = 0;
   while (g_SysInfo->uptime_seconds < sec)
   {
      /* Update uptime from tick counter */
      g_SysInfo->uptime_seconds = system_ticks / 1000;
      if (g_SysInfo->uptime_seconds != last_uptime)
      {
         printf("\r\x1B[1;37;46mSystem up for %u seconds\x1B[0m",
                g_SysInfo->uptime_seconds);
         last_uptime = g_SysInfo->uptime_seconds;
      }

      /* Idle efficiently until next interrupt: enable interrupts, HLT,
         then disable again. Matches i686 PS/2 idle usage. */
      uint8_t interrupts_were_enabled = g_HalIoOperations->EnableInterrupts();
      g_HalIoOperations->iowait();
      if (!interrupts_were_enabled)
      {
         g_HalIoOperations->DisableInterrupts();
      }
   }
   printf("\n");
}

void __attribute__((noreturn)) start(BOOT_Info *boot)
{
   // Init system
   memset(&__bss_start, 0, (&__end) - (&__bss_start));
   _init();

   memset(g_SysInfo, 0, sizeof(SYS_Info));

   /* Member-wise copy of the pre-parsed boot parameters into g_SysInfo. */
   g_SysInfo->boot = *boot;

   MEM_Initialize();
   TTY_Initialize();
   SYS_Initialize();
   CPU_Initialize();
   HAL_Initialize();
   CmdLine_Initialize();

   TTY_Device *tty_dev = TTY_GetDevice();
   TTY_Flush(tty_dev);

   if (!FS_Initialize())
   {
      TTY_Flush(tty_dev);
      goto end;
   }
   if (!Init_MountRoot())
   {
      TTY_Flush(tty_dev);
      goto end;
   }
   VFS_SelfTest();
   Keyboard_Initialize();

   if (!Dylib_Initialize())
   {
      TTY_Flush(tty_dev);
   }

   /* Mark system as fully initialized */
   SYS_Finalize();

   logfmt(LOG_WARNING,
          "[INIT] scheduler returned to kernel after launching /usr/bin/sh\n");
   // TTY_SetVideoMode(80, 43);
   // TTY_SetVideoMode(40, 25);

   /* Fallback interactive mode if shell handoff returns unexpectedly. */
   interact();

   hold(-1);

end:
   for (;;);
}
