// SPDX-License-Identifier: GPL-3.0-only

#include "process.h"
#include <hal/paging.h>
#include <hal/scheduler.h>
#include <mem/mm_kernel.h>
#include <mem/mm_proc.h>
#include <std/stdio.h>
#include <std/string.h>

#define USER_STACK_TOP 0xBFFF0000u
#define USER_STACK_SIZE (64u * 1024u)
#define USER_HEAP_START 0x10000000u
#define USER_EXIT_TRAMPOLINE_VA 0xBFFF1000u

typedef struct
{
   unsigned char e_ident[16];
   uint16_t e_type;
   uint16_t e_machine;
   uint32_t e_version;
   uint32_t e_entry;
   uint32_t e_phoff;
   uint32_t e_shoff;
   uint32_t e_flags;
   uint16_t e_ehsize;
   uint16_t e_phentsize;
   uint16_t e_phnum;
   uint16_t e_shentsize;
   uint16_t e_shnum;
   uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct
{
   uint32_t p_type;
   uint32_t p_offset;
   uint32_t p_vaddr;
   uint32_t p_paddr;
   uint32_t p_filesz;
   uint32_t p_memsz;
   uint32_t p_flags;
   uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

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
#define PT_LOAD 1

#define EXEC_MAX_ARGS 64
#define EXEC_MAX_ENVP 64
#define EXEC_MAX_STR 4096

static int map_user_trampoline(Process *proc)
{
   uint32_t phys = PMM_AllocatePhysicalPage();
   if (!phys) return -1;

   if (!g_HalPagingOperations->MapPage(proc->page_directory,
                                       USER_EXIT_TRAMPOLINE_VA, phys,
                                       HAL_PAGE_PRESENT | HAL_PAGE_RW |
                                           HAL_PAGE_USER))
   {
      PMM_FreePhysicalPage(phys);
      return -1;
   }

   void *kernel_pd = Process_GetKernelPageDirectory();
   if (!kernel_pd) kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();

   g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);
   memset((void *)USER_EXIT_TRAMPOLINE_VA, 0, PAGE_SIZE);
   ((uint8_t *)USER_EXIT_TRAMPOLINE_VA)[0] = 0xEB;
   ((uint8_t *)USER_EXIT_TRAMPOLINE_VA)[1] = 0xFE;
   g_HalPagingOperations->SwitchPageDirectory(kernel_pd);

   return 0;
}

static bool load_header(VFS_File *file, Elf32_Ehdr *ehdr)
{
   if (!VFS_Seek(file, 0)) return false;

   if (VFS_Read(file, sizeof(*ehdr), ehdr) != sizeof(*ehdr)) return false;

   if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
       ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3)
   {
      return false;
   }

   if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB)
   {
      return false;
   }

   if (ehdr->e_machine != EM_386) return false;

   return true;
}

static int load_segments_into_directory(VFS_File *file, void *page_directory,
                                        const Elf32_Ehdr *ehdr)
{
   Elf32_Phdr phdr;

   for (uint16_t i = 0; i < ehdr->e_phnum; ++i)
   {
      uint32_t phoff = ehdr->e_phoff + (i * ehdr->e_phentsize);
      if (!VFS_Seek(file, phoff)) return -1;
      if (VFS_Read(file, sizeof(phdr), &phdr) != sizeof(phdr)) return -1;

      if (phdr.p_type != PT_LOAD) continue;

      uint32_t vaddr = phdr.p_vaddr;
      uint32_t memsz = phdr.p_memsz;
      uint32_t filesz = phdr.p_filesz;

      uint32_t start = vaddr & ~0xFFFu;
      uint32_t end = vaddr + memsz;
      uint32_t pages = (end - start + PAGE_SIZE - 1) / PAGE_SIZE;

      for (uint32_t p = 0; p < pages; ++p)
      {
         uint32_t page_va = start + (p * PAGE_SIZE);
         uint32_t phys = PMM_AllocatePhysicalPage();
         if (!phys) return -1;

         if (!g_HalPagingOperations->MapPage(page_directory, page_va, phys,
                                             HAL_PAGE_PRESENT | HAL_PAGE_RW |
                                                 HAL_PAGE_USER))
         {
            PMM_FreePhysicalPage(phys);
            return -1;
         }
      }

      if (!VFS_Seek(file, phdr.p_offset)) return -1;

      uint8_t *buffer = (uint8_t *)kmalloc(512);
      if (!buffer) return -1;

      uint32_t remaining = filesz;
      uint32_t offset = 0;
      void *kernel_pd = Process_GetKernelPageDirectory();
      if (!kernel_pd)
      {
         kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();
      }

      while (remaining > 0)
      {
         uint32_t chunk = remaining < 512 ? remaining : 512;
         uint32_t got = VFS_Read(file, chunk, buffer);
         if (got == 0 || got > chunk)
         {
            free(buffer);
            return -1;
         }

         g_HalPagingOperations->SwitchPageDirectory(page_directory);
         memcpy((void *)(vaddr + offset), buffer, got);
         g_HalPagingOperations->SwitchPageDirectory(kernel_pd);

         offset += got;
         remaining -= got;
      }

      free(buffer);

      if (memsz > filesz)
      {
         void *kernel_pd = Process_GetKernelPageDirectory();
         if (!kernel_pd)
         {
            kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();
         }

         g_HalPagingOperations->SwitchPageDirectory(page_directory);
         memset((void *)(vaddr + filesz), 0, memsz - filesz);
         g_HalPagingOperations->SwitchPageDirectory(kernel_pd);
      }
   }

   return 0;
}

static void free_vector_copy(char **vec, int count)
{
   if (!vec) return;
   for (int i = 0; i < count; ++i)
   {
      if (vec[i]) free(vec[i]);
   }
}

static int copy_string_vector(const char *const in[], char **out, int max_count)
{
   int count = 0;

   if (!in)
   {
      out[0] = NULL;
      return 0;
   }

   while (in[count] && count < max_count)
   {
      unsigned len = strlen(in[count]);
      if (len >= EXEC_MAX_STR) return -1;

      out[count] = (char *)kmalloc(len + 1);
      if (!out[count]) return -1;

      memcpy(out[count], in[count], len + 1);
      ++count;
   }

   if (count == max_count && in[count] != NULL) return -1;

   out[count] = NULL;
   return count;
}

static int build_initial_user_stack(Process *proc, const char *const argv[],
                                    const char *const envp[])
{
   char *argv_copy[EXEC_MAX_ARGS + 1] = {0};
   char *envp_copy[EXEC_MAX_ENVP + 1] = {0};
   uint32_t argv_ptrs[EXEC_MAX_ARGS];
   uint32_t envp_ptrs[EXEC_MAX_ENVP];

   int argc = copy_string_vector(argv, argv_copy, EXEC_MAX_ARGS);
   if (argc < 0) return -1;

   int envc = copy_string_vector(envp, envp_copy, EXEC_MAX_ENVP);
   if (envc < 0)
   {
      free_vector_copy(argv_copy, argc > 0 ? argc : 0);
      return -1;
   }

   uint32_t sp = proc->stack_end;

   void *kernel_pd = Process_GetKernelPageDirectory();
   if (!kernel_pd) kernel_pd = g_HalPagingOperations->GetCurrentPageDirectory();

   g_HalPagingOperations->SwitchPageDirectory(proc->page_directory);

   for (int i = envc - 1; i >= 0; --i)
   {
      unsigned len = strlen(envp_copy[i]) + 1;
      sp -= len;
      memcpy((void *)sp, envp_copy[i], len);
      envp_ptrs[i] = sp;
   }

   for (int i = argc - 1; i >= 0; --i)
   {
      unsigned len = strlen(argv_copy[i]) + 1;
      sp -= len;
      memcpy((void *)sp, argv_copy[i], len);
      argv_ptrs[i] = sp;
   }

   sp &= ~0x3u;

   sp -= sizeof(uint32_t);
   *(uint32_t *)sp = 0;
   for (int i = envc - 1; i >= 0; --i)
   {
      sp -= sizeof(uint32_t);
      *(uint32_t *)sp = envp_ptrs[i];
   }
   uint32_t envp_user = sp;

   sp -= sizeof(uint32_t);
   *(uint32_t *)sp = 0;
   for (int i = argc - 1; i >= 0; --i)
   {
      sp -= sizeof(uint32_t);
      *(uint32_t *)sp = argv_ptrs[i];
   }
   uint32_t argv_user = sp;

   sp -= sizeof(uint32_t);
   *(uint32_t *)sp = envp_user;
   sp -= sizeof(uint32_t);
   *(uint32_t *)sp = argv_user;
   sp -= sizeof(uint32_t);
   *(uint32_t *)sp = (uint32_t)argc;

   g_HalPagingOperations->SwitchPageDirectory(kernel_pd);

   proc->esp = sp;
   proc->ebp = sp;

   free_vector_copy(argv_copy, argc);
   free_vector_copy(envp_copy, envc);

   return 0;
}

int Process_Execute(Process *proc, const char *path, const char *const argv[],
                    const char *const envp[])
{
   if (!proc || !path) return -1;
   if (proc->kernel_mode) return -1;

   VFS_File *file = VFS_Open(path);
   if (!file) return -2;

   Elf32_Ehdr ehdr;
   if (!load_header(file, &ehdr))
   {
      VFS_Close(file);
      return -3;
   }

   void *new_pd = g_HalPagingOperations->CreatePageDirectory();
   if (!new_pd)
   {
      VFS_Close(file);
      return -1;
   }

   Process staged = *proc;
   staged.page_directory = new_pd;
   staged.heap_start = staged.heap_end = 0;
   staged.stack_start = staged.stack_end = 0;

   if (Heap_ProcessInitialize(&staged, USER_HEAP_START) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(new_pd);
      VFS_Close(file);
      return -1;
   }

   if (Stack_ProcessInitialize(&staged, USER_STACK_TOP, USER_STACK_SIZE) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(new_pd);
      VFS_Close(file);
      return -1;
   }

   if (map_user_trampoline(&staged) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(new_pd);
      VFS_Close(file);
      return -1;
   }

   if (load_segments_into_directory(file, new_pd, &ehdr) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(new_pd);
      VFS_Close(file);
      return -1;
   }

   VFS_Close(file);

   if (build_initial_user_stack(&staged, argv, envp) != 0)
   {
      g_HalPagingOperations->DestroyPageDirectory(new_pd);
      return -1;
   }

   void *old_pd = proc->page_directory;

   proc->page_directory = staged.page_directory;
   proc->heap_start = staged.heap_start;
   proc->heap_end = staged.heap_end;
   proc->stack_start = staged.stack_start;
   proc->stack_end = staged.stack_end;
   proc->eip = ehdr.e_entry;
   proc->esp = staged.esp;
   proc->ebp = staged.ebp;
   proc->eax = proc->ebx = proc->ecx = proc->edx = 0;
   proc->esi = proc->edi = 0;
   proc->eflags = 0x202u;
   proc->saved_regs = NULL;

   if (old_pd) g_HalPagingOperations->DestroyPageDirectory(old_pd);

   logfmt(LOG_INFO, "[PROC] exec: pid=%u path=%s entry=0x%08x\n", proc->pid,
          path, proc->eip);

    if (g_HalSchedulerOperations && g_HalSchedulerOperations->ContextSwitch)
    {
       g_HalSchedulerOperations->ContextSwitch();
    }

   return 0;
}
