// SPDX-License-Identifier: GPL-3.0-only

#include <drivers/tty/tty.h>
#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <valecium/fs.h>

void interact(void)
{
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
         if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';

         if (strcmp(buf, "exit") == 0)
         {
            break;
         }
         else if (strcmp(buf, "shutdown") == 0)
         {
            printf("Shutting down...\n");
            g_HalIoOperations->DisableInterrupts();
            g_HalIoOperations->Halt();
            break;
         }
         else if (strcmp(buf, "reboot") == 0)
         {
            printf("Rebooting...\n");
            /* Load invalid IDT and trigger interrupt to cause triple fault */
            uint32_t invalid_idt[2] = {0, 0};
            __asm__ volatile("lidt %0" : : "m"(invalid_idt));
            __asm__ volatile("int $0");
         }
         else if (strcmp(buf, "read") == 0 || strncmp(buf, "read ", 5) == 0)
         {
            char *path = buf + 4;
            while (*path == ' ') path++;

            if (*path == '\0')
            {
               printf("Usage: read <file-path>\n\n");
               printf("$ ");
               continue;
            }

            VFS_File *f = VFS_Open(path);
            if (f)
            {
               if (f->is_directory)
               {
                  printf("Error: '%s' is a directory. Use: ls %s\n", path,
                         path);
               }
               else
               {
                  char *read_buf = kmalloc(4096);
                  if (read_buf)
                  {
                     uint32_t bytes;
                     uint32_t total_read = 0;
                     uint32_t max_read =
                         65536; /* Limit to 64KB to prevent runaway output */
                     while ((bytes = VFS_Read(f, 4096, read_buf)) > 0 &&
                            total_read < max_read)
                     {
                        for (uint32_t i = 0; i < bytes; i++)
                        {
                           printf("%c", read_buf[i]);
                        }
                        total_read += bytes;
                     }
                     free(read_buf);
                  }
                  else
                  {
                     printf("Error: Out of memory\n");
                  }
               }
               VFS_Close(f);
            }
            else
            {
               printf("Error: Could not open file '%s'\n", path);
            }
            printf("\n");
         }
         else if (strcmp(buf, "ls") == 0 || strncmp(buf, "ls ", 3) == 0)
         {
            char *path = buf + 2;
            while (*path == ' ') path++;
            if (*path == '\0') path = "/";

            VFS_File *dir = VFS_OpenDir(path);
            if (dir)
            {
               VFS_DirEntry entry;
               uint32_t entry_count = 0;
               uint32_t max_entries = 4096;

               while (entry_count < max_entries &&
                      VFS_ReadDir(dir, &entry) == VFS_OK)
               {
                  printf("%s%s  (%u bytes)\n", entry.name,
                         entry.is_directory ? "/" : "", entry.size);
                  entry_count++;
               }

               if (entry_count == 0) printf("(empty directory)\n");
               if (entry_count >= max_entries)
                  printf("Warning: directory listing truncated\n");

               VFS_Close(dir);
            }
            else
            {
               printf("Error: Could not open directory '%s'\n", path);
            }
            printf("\n");
         }
         else
         {
            printf("You typed: %s\n", buf);
         }
         if (buf[n - 1] != '\n') printf("\n");
         printf("$ ");
      }
      else
      {
         /* Wait for interrupt/input */
         uint8_t interrupts_were_enabled =
             g_HalIoOperations->EnableInterrupts();
         g_HalIoOperations->iowait();
         if (!interrupts_were_enabled)
         {
            g_HalIoOperations->DisableInterrupts();
         }
      }
   }
   free(buf);
}