// SPDX-License-Identifier: GPL-3.0-only

#include <cpu/cpu.h>
#include <cpu/process.h>
#include <crypto/crypto.h>
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
#include <sys/kmod/dylib.h>
#include <sys/elf.h>
#include <sys/sys.h>
#include <valecium/fs.h>
#include <valecium/system.h>

#include <libmath/math.h>

extern int Init_MountRoot(void);
extern void interact();
static void __attribute__((unused, noreturn)) fallback(void);

extern uint8_t __bss_start;
extern uint8_t __end;
extern void _init();

void hold(int sec)
{
   uint32_t last_uptime = 0;
   int run_forever = (sec < 0);
   while (run_forever || g_SysInfo->uptime_seconds < (uint64_t)sec)
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
   memset(&__bss_start, 0, (&__end) - (&__bss_start));
   memset(g_SysInfo, 0, sizeof(SYS_Info));

   g_SysInfo->boot = *boot;

   MEM_Initialize();
   TTY_Initialize();
   SYS_Initialize();
   CPU_Initialize();
   HAL_Initialize();
   CmdLine_Initialize();
   Crypto_SelfTest();

   if (FS_Initialize() < 0)
   {
      goto end;
   }
   if (Init_MountRoot() < 0)
   {
      goto end;
   }
   VFS_SelfTest();
   Keyboard_Initialize();

   if (Dylib_Initialize() < 0)
   {
   }

   SYS_Finalize();

   Process_SelfTest();

   Process *shell_proc = ELF_LoadProcess("/usr/bin/selftest", false);
   if (!shell_proc)
   {
      logfmt(LOG_ERROR, "[INIT] failed to load init process\n");
      goto backup;
   }

   if (g_HalSchedulerOperations && g_HalSchedulerOperations->ContextSwitch)
   {
      g_HalSchedulerOperations->ContextSwitch();
   }

backup:
   hold(-1);

end:
   for (;;);
}

static void __attribute__((unused, noreturn)) fallback(void)
{
   hold(-1);

   for (;;)
   {
   }
}
