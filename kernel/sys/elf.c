// SPDX-License-Identifier: GPL-3.0-only
#include "elf.h"
#include <cpu/process.h>
#include <hal/paging.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>

#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EM_386 3

bool ELF_Load(VFS_File *file, void **entryOut)
{
   // read ELF header
   if (!VFS_Seek(file, 0))
   {
      printf("ELF: seek header failed\n");
      return false;
   }

   /* Use a heap bounce buffer to avoid stack overwrites if a buggy driver
    * writes past the requested length. */
   Elf32_Ehdr ehdr;
   void *hdr_buf = kmalloc(sizeof(ehdr));
   if (!hdr_buf)
   {
      printf("ELF: failed to allocate header buffer\n");
      return false;
   }

   uint32_t hdr_read = VFS_Read(file, sizeof(ehdr), hdr_buf);
   if (hdr_read != sizeof(ehdr))
   {
      printf("ELF: read header failed (got %u)\n", hdr_read);
      free(hdr_buf);
      return false;
   }

   memcpy(&ehdr, hdr_buf, sizeof(ehdr));
   free(hdr_buf);

   // validate magic and class
   if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3)
   {
      printf("ELF: bad magic\n");
      return false;
   }

   if (ehdr.e_ident[4] != ELFCLASS32 || ehdr.e_ident[5] != ELFDATA2LSB)
   {
      printf("ELF: unsupported ELF class or endian\n");
      return false;
   }

   if (ehdr.e_machine != EM_386)
   {
      printf("ELF: unsupported machine\n");
      return false;
   }

   // read program headers
   if (ehdr.e_phnum == 0 || ehdr.e_phentsize != sizeof(Elf32_Phdr))
   {
      printf("ELF: no program headers or unexpected phentsize\n");
      return false;
   }

   // allocate temporary buffer for program headers (small count expected)
   Elf32_Phdr phdr;

   for (uint16_t i = 0; i < ehdr.e_phnum; i++)
   {
      uint32_t phoff = ehdr.e_phoff + i * ehdr.e_phentsize;
      if (!VFS_Seek(file, phoff))
      {
         printf("ELF: seek phdr %u failed\n", i);
         return false;
      }

      void *ph_buf = kmalloc(sizeof(phdr));
      if (!ph_buf)
      {
         printf("ELF: alloc phdr buffer failed\n");
         return false;
      }

      uint32_t ph_read = VFS_Read(file, sizeof(phdr), ph_buf);
      if (ph_read != sizeof(phdr))
      {
         printf("ELF: read phdr %u failed (got %u)\n", i, ph_read);
         free(ph_buf);
         return false;
      }

      memcpy(&phdr, ph_buf, sizeof(phdr));
      free(ph_buf);

      const uint32_t PT_LOAD = 1;
      if (phdr.p_type != PT_LOAD) continue;

      // determine destination address (prefer physical p_paddr if provided)
      uint8_t *dest = (uint8_t *)(phdr.p_paddr ? phdr.p_paddr : phdr.p_vaddr);

      // read file data for this segment
      uint32_t remaining = phdr.p_filesz;
      uint32_t fileOffset = phdr.p_offset;
      const uint32_t CHUNK =
          512; // FAT sector size, read in sector-sized chunks

      if (remaining > 0)
      {
         if (!VFS_Seek(file, fileOffset))
         {
            printf("ELF: seek segment data failed\n");
            return false;
         }

         while (remaining > 0)
         {
            uint32_t toRead = remaining > CHUNK ? CHUNK : remaining;
            uint32_t got = VFS_Read(file, toRead, dest);
            if (got == 0)
            {
               printf("ELF: short read for segment\n");
               return false;
            }

            dest += got;
            remaining -= got;
         }
      }

      // zero the rest for bss
      if (phdr.p_memsz > phdr.p_filesz)
      {
         uint32_t zeros = phdr.p_memsz - phdr.p_filesz;
         memset(dest, 0, zeros);
      }
   }

   // return entry point
   *entryOut = (void *)ehdr.e_entry;
   return true;
}

Process *ELF_LoadProcess(const char *filename, bool kernel_mode)
{
   if (!filename) return NULL;
   // Open ELF file from filesystem
   VFS_File *file = VFS_Open(filename);
   if (!file)
   {
      logfmt(LOG_ERROR, "[ELF] LoadProcess: VFS_Open failed for %s\n",
             filename);
      return NULL;
   }

   // Read ELF header
   if (!VFS_Seek(file, 0))
   {
      logfmt(LOG_ERROR, "[ELF] LoadProcess: seek header failed\n");
      VFS_Close(file);
      return NULL;
   }

   Elf32_Ehdr ehdr;
   void *hdr_buf = kmalloc(sizeof(ehdr));
   if (!hdr_buf)
   {
      logfmt(LOG_ERROR, "[ELF] LoadProcess: read header failed\n");
      VFS_Close(file);
      return NULL;
   }

   uint32_t read_bytes = VFS_Read(file, sizeof(ehdr), (uint8_t *)hdr_buf);
   if (read_bytes != sizeof(ehdr))
   {
      printf("[ELF] LoadProcess: read header failed\n");
      free(hdr_buf);
      VFS_Close(file);
      return NULL;
   }

   memcpy(&ehdr, hdr_buf, sizeof(ehdr));
   free(hdr_buf);
   // Validate magic
   if (ehdr.e_ident[EI_MAG0] != ELFMAG0 || ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr.e_ident[EI_MAG2] != ELFMAG2 || ehdr.e_ident[EI_MAG3] != ELFMAG3)
   {
      logfmt(LOG_ERROR, "[ELF] LoadProcess: bad magic\n");
      VFS_Close(file);
      return NULL;
   }
   // Create process with ELF entry point
   Process *proc = Process_Create(ehdr.e_entry, kernel_mode);
   if (!proc)
   {
      logfmt(LOG_ERROR, "[ELF] LoadProcess: Process_Create failed\n");
      VFS_Close(file);
      return NULL;
   }

   // Save kernel page directory at the start - we'll need to restore it
   void *kernel_pdir = g_HalPagingOperations->GetCurrentPageDirectory();

   // Load each program header (PT_LOAD segments)
   Elf32_Phdr phdr;
   for (uint16_t i = 0; i < ehdr.e_phnum; ++i)
   {
      uint32_t phoff = ehdr.e_phoff + i * ehdr.e_phentsize;
      if (!VFS_Seek(file, phoff))
      {
         logfmt(LOG_ERROR, "[ELF] LoadProcess: seek phdr %u failed\n", i);
         Process_Destroy(proc);
         VFS_Close(file);
         return NULL;
      }

      void *ph_buf = kmalloc(sizeof(phdr));
      if (!ph_buf)
      {
         logfmt(LOG_ERROR, "[ELF] LoadProcess: read phdr %u failed\n", i);
         Process_Destroy(proc);
         VFS_Close(file);
         return NULL;
      }

      uint32_t ph_read = VFS_Read(file, sizeof(phdr), (uint8_t *)ph_buf);
      if (ph_read != sizeof(phdr))
      {
         printf("[ELF] LoadProcess: read phdr %u failed (got %u)\n", i,
                ph_read);
         free(ph_buf);
         Process_Destroy(proc);
         VFS_Close(file);
         return NULL;
      }

      memcpy(&phdr, ph_buf, sizeof(phdr));
      free(ph_buf);

      // Only load PT_LOAD segments
      const uint32_t PT_LOAD = 1;
      if (phdr.p_type != PT_LOAD) continue;

      uint32_t vaddr = phdr.p_vaddr;
      uint32_t memsz = phdr.p_memsz;
      uint32_t filesz = phdr.p_filesz;

      // Calculate page-aligned start and total pages needed
      uint32_t vaddr_aligned = vaddr & ~0xFFFu; // Align down to page boundary
      uint32_t vaddr_end = vaddr + memsz;       // End address (exclusive)
      uint32_t pages_needed = (vaddr_end - vaddr_aligned + 4095) / 4096;

      for (uint32_t j = 0; j < pages_needed; ++j)
      {
         uint32_t page_va = vaddr_aligned + (j * 4096);
         uint32_t phys = PMM_AllocatePhysicalPage();
         if (phys == 0)
         {
            logfmt(LOG_ERROR,
                   "[ELF] LoadProcess: PMM_AllocatePhysicalPage failed\n");
            Process_Destroy(proc);
            VFS_Close(file);
            return NULL;
         }

         // Map page into process's page directory (user mode, read+write)
         if (!g_HalPagingOperations->MapPage(
                 proc->page_directory, page_va, phys,
                 HAL_PAGE_PRESENT | HAL_PAGE_RW | HAL_PAGE_USER))
         {
            logfmt(LOG_ERROR,
                   "[ELF] LoadProcess: HAL_Paging_MapPage failed at 0x%08x\n",
                   page_va);
            PMM_FreePhysicalPage(phys);
            Process_Destroy(proc);
            VFS_Close(file);
            return NULL;
         }
      }

      // Read segment data from file and copy to process memory
      if (!VFS_Seek(file, phdr.p_offset))
      {
         logfmt(LOG_ERROR, "[ELF] LoadProcess: seek segment data failed\n");
         Process_Destroy(proc);
         VFS_Close(file);
         return NULL;
      }

      // Read file data into kernel buffer (while still in kernel page
      // directory). Use a heap bounce buffer to avoid stack overwrite if the
      // driver returns more than requested.
      const uint32_t CHUNK = 512;
      uint8_t *buffer = (uint8_t *)kmalloc(CHUNK);
      if (!buffer)
      {
         printf("[ELF] LoadProcess: alloc buffer failed\n");
         Process_Destroy(proc);
         VFS_Close(file);
         return NULL;
      }

      uint32_t remaining = filesz;
      uint32_t offset = 0;
      void *old_pdir = g_HalPagingOperations->GetCurrentPageDirectory();

      while (remaining > 0)
      {
         uint32_t chunk = remaining < CHUNK ? remaining : CHUNK;
         uint32_t bytes_read = VFS_Read(file, chunk, buffer);
         if (bytes_read == 0 || bytes_read > chunk)
         {
            logfmt(LOG_ERROR, "[ELF] LoadProcess: VFS_Read failed\n");
            free(buffer);
            Process_Destroy(proc);
            VFS_Close(file);
            return NULL;
         }

         // Temporarily switch to process page directory to write to its memory
         void *old_pdir = g_HalPagingOperations->GetCurrentPageDirectory();
         g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);

         // Copy to process memory
         memcpy((void *)(vaddr + offset), buffer, bytes_read);

         // Immediately restore kernel page directory
         g_HalPagingOperations->SwitchPageDirectory(old_pdir);

         offset += bytes_read;
         remaining -= bytes_read;
      }

      free(buffer);

      // Zero out BSS (memsz > filesz)
      if (memsz > filesz)
      {
         // Temporarily switch to process page directory for BSS zeroing
         void *old_pdir = g_HalPagingOperations->GetCurrentPageDirectory();
         g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);

         memset((void *)(vaddr + filesz), 0, memsz - filesz);

         // Restore kernel page directory
         g_HalPagingOperations->SwitchPageDirectory(old_pdir);
      }
   }

   VFS_Close(file);
   logfmt(LOG_INFO,
          "[ELF] LoadProcess: successfully loaded %s into pid=%u at entry "
          "0x%08x\n",
          filename, proc->pid, ehdr.e_entry);

   // Force restore kernel page directory to ensure we're back in kernel space
   g_HalPagingOperations->SwitchPageDirectory(kernel_pdir);

   return proc;
}
