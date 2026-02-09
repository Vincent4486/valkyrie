// SPDX-License-Identifier: GPL-3.0-only

#include <cpu/cpu.h>
#include <cpu/process.h>
#include <drivers/ata/ata.h>
#include <hal/hal.h>
#include <hal/tty.h>
#include <hal/irq.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>
#include <sys/dylib.h>
#include <sys/elf.h>
#include <sys/sys.h>
#include <valkyrie/fs.h>
#include <valkyrie/system.h>
#include <drivers/tty/tty.h>
#include <drivers/keyboard/keyboard.h>
#include <fs/devfs/devfs.h>
#include <fs/fd/fd.h>

#include <libmath/math.h>

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
         printf("\r\x1B[1;37;46mSystem up for %u seconds\x1B[0m", g_SysInfo->uptime_seconds);
         last_uptime = g_SysInfo->uptime_seconds;
      }

      /* Idle efficiently until next interrupt: enable interrupts, HLT,
         then disable again. Matches i686 PS/2 idle usage. */
      __asm__ volatile("sti; hlt; cli");
   }
   printf("\n");
}

void interact(void){
   printf("\nInteractive Mode. Type 'exit' to stop.\n$ ");
   
   char *buf = kmalloc(512);
   if (!buf) return;
   
   TTY_Device *tty_dev = TTY_GetDevice();
   
   for (;;)
   {
      int n = TTY_Read(tty_dev, buf, 511);
      if (n > 0)
      {
         buf[n] = '\0';
         /* Trim trailing newline */
         if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';

         if (strcmp(buf, "exit") == 0) {
             break;
         }
         else if (strcmp(buf, "shutdown") == 0) {
             printf("Shutting down...\n");
             __asm__ volatile("hlt");
             break;
         }
         else if (strcmp(buf, "reboot") == 0) {
             printf("Rebooting...\n");
             /* Load invalid IDT and trigger interrupt to cause triple fault */
             uint32_t invalid_idt[2] = {0, 0};
             __asm__ volatile("lidt %0" : : "m"(invalid_idt));
             __asm__ volatile("int $0");
         }
         else if (strncmp(buf, "read ", 5) == 0) {
             char *path = buf + 5;
             while (*path == ' ') path++;
             
             VFS_File *f = VFS_Open(path);
             if (f) {
                 char *read_buf = kmalloc(512);
                 if (read_buf) {
                     uint32_t bytes;
                     uint32_t total_read = 0;
                     uint32_t max_read = 4096; /* Limit to 4KB to prevent infinite reads */
                     while ((bytes = VFS_Read(f, 511, read_buf)) > 0 && total_read < max_read) {
                         read_buf[bytes] = '\0';
                         printf("%s", read_buf);
                         total_read += bytes;
                     }
                     free(read_buf);
                 } else {
                     printf("Error: Out of memory\n");
                 }
                 VFS_Close(f);
             } else {
                 printf("Error: Could not open file '%s'\n", path);
             }
             printf("\n");
         }
         else {
             printf("You typed: %s\n", buf);
         }
         if (buf[n-1] != '\n') printf("\n");
         printf("$ ");
      }
      else
      {
          /* Wait for interrupt/input */
          __asm__ volatile("sti; hlt; cli");
      }
   }
   free(buf);
}

void perform_mount(void){
   FS_Mount(&g_SysInfo->volume[0], "/");
   VFS_SelfTest();
}


void __attribute__((section(".entry"))) start(uint16_t bootDrive,
                                              void *multiboot_info_ptr)
{
   // Init system
   memset(&__bss_start, 0, (&__end) - (&__bss_start));
   _init();

   memset(g_SysInfo, 0, sizeof(SYS_Info));
   g_SysInfo->boot_device = bootDrive;

   MEM_Initialize(multiboot_info_ptr);
   TTY_Initialize();
   Keyboard_Initialize();
   SYS_Initialize();
   CPU_Initialize();
   HAL_Initialize();

   TTY_Device *tty_dev = TTY_GetDevice();
   TTY_Flush(tty_dev);

   if (!FS_Initialize())
   {
      printf("FS initialization failed\n");
      goto end;
   }
   perform_mount();

   if (!Dylib_Initialize())
   {
      printf("Failed to load dynamic libraries...");
      goto end;
   }

   /* Mark system as fully initialized */
   SYS_Finalize();

   ELF_LoadProcess("/usr/bin/sh", false);

   /* Start interactive line reader: on ENTER, print the entered text. */
   interact();

   hold(-1);

end:
   for (;;);
}
