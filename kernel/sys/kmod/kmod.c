// SPDX-License-Identifier: GPL-3.0-only

/* Dynamic link helpers for the kernel. Supports true ELF dynamic linking
 * with PLT/GOT relocation.
 *
 * Library metadata lives in kernel-owned memory (BSS registry + heap-backed
 * images) rather than fixed low-memory physical addresses.
 */

#include "kmod.h"
#include <hal/mem.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>
#include <sys/elf.h>
#include <sys/sys.h>
#include <valecium/fs.h>

// ELF32 relocation types (i686)
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

// ELF32 structures for parsing at runtime
typedef struct
{
   uint32_t r_offset;
   uint32_t r_info;
} Elf32_Rel;

typedef struct
{
   uint32_t r_offset;
   uint32_t r_info;
   int32_t r_addend;
} Elf32_Rela;

// ELF32 Symbol table entry
typedef struct
{
   uint32_t st_name;
   uint32_t st_value;
   uint32_t st_size;
   uint8_t st_info;
   uint8_t st_other;
   uint16_t st_shndx;
} Elf32_Sym;

#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

// ELF section header entry
typedef struct
{
   uint32_t sh_name;
   uint32_t sh_type;
   uint32_t sh_flags;
   uint32_t sh_addr;
   uint32_t sh_offset;
   uint32_t sh_size;
   uint32_t sh_link;
   uint32_t sh_info;
   uint32_t sh_addralign;
   uint32_t sh_entsize;
} Elf32_Shdr;

// Section types
#define SHT_SYMTAB 2
#define SHT_DYNSYM 11
#define SHT_STRTAB 3

// Extended library data (kept separately from the base LibRecord registry)
typedef struct
{
   DependencyRecord deps[KMOD_MAX_DEPS];
   int dep_count;
   SymbolRecord symbols[KMOD_MAX_SYMBOLS];
   int symbol_count;

   // ELF dynamic section metadata (parsed from .dynamic at load time)
   uint32_t dynsym_addr; // Address of .dynsym section in loaded memory
   uint32_t dynsym_size; // Size in bytes
   uint32_t dynstr_addr; // Address of .dynstr section in loaded memory
   uint32_t dynstr_size; // Size in bytes
   uint32_t rel_addr;    // Address of .rel.dyn relocations
   uint32_t rel_size;    // Size of .rel.dyn
   uint32_t jmprel_addr; // Address of .rel.plt (PLT relocations)
   uint32_t jmprel_size; // Size of .rel.plt
   uint32_t pltgot_addr; // Address of .got.plt (for PLT patching)

   uint32_t alloc_size; // Heap bytes reserved for this module image
   int loaded;          // 1 if loaded in memory, 0 if not
} ExtendedLibData;

// Memory allocator state
static int kmod_mem_initialized = 0;
static uint32_t kmod_total_allocated = 0;
static LibRecord s_lib_registry[LIB_REGISTRY_MAX];
static ExtendedLibData extended_data[LIB_REGISTRY_MAX];

// Global symbol table - shared across all loaded libraries and kernel
static GlobalSymbolEntry global_symtab[KMOD_MAX_GLOBAL_SYMBOLS];
static int global_symtab_count = 0;

// Forward declarations
static int parse_elf_symbols(ExtendedLibData *ext, uint32_t base_addr,
                             uint32_t size);
static int find_index(const char *name);

static kmod_register_symbols_t symbol_callback = NULL;

int KMOD_MemoryInitialize(void)
{
   if (kmod_mem_initialized) return 0;

   memset(s_lib_registry, 0, sizeof(s_lib_registry));

   // Clear extended data
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      extended_data[i].dep_count = 0;
      extended_data[i].symbol_count = 0;
      extended_data[i].dynsym_addr = 0;
      extended_data[i].dynstr_addr = 0;
      extended_data[i].rel_addr = 0;
      extended_data[i].jmprel_addr = 0;
      extended_data[i].alloc_size = 0;
      extended_data[i].loaded = 0;
   }

   kmod_total_allocated = 0;
   kmod_mem_initialized = 1;

   uint32_t heap_start = (uint32_t)mem_heap_start();
   uint32_t heap_end = (uint32_t)mem_heap_end();
   uint32_t heap_size =
       (heap_end >= heap_start) ? (heap_end - heap_start + 1) : 0;

   logfmt(LOG_INFO,
          "[KMOD] Heap-backed module allocator initialized: 0x%x - 0x%x "
          "(%d MiB)\n",
          heap_start, heap_end, heap_size / 0x100000);

   return 0;
}

// ============================================================================
// Global Symbol Table Management
// ============================================================================

int KMOD_AddGlobalSymbol(const char *name, uint32_t address,
                          const char *lib_name, int is_kernel)
{
   if (global_symtab_count >= KMOD_MAX_GLOBAL_SYMBOLS)
   {
      logfmt(LOG_ERROR, "[ERROR] Global symbol table full (%d entries)\n",
             KMOD_MAX_GLOBAL_SYMBOLS);
      return -1;
   }

   GlobalSymbolEntry *entry = &global_symtab[global_symtab_count];
   strncpy(entry->name, name, 63);
   entry->name[63] = '\0';
   entry->address = address;
   strncpy(entry->lib_name, lib_name, 63);
   entry->lib_name[63] = '\0';
   entry->is_kernel = is_kernel;

   global_symtab_count++;
   return 0;
}

uint32_t KMOD_LookupGlobalSymbol(const char *name)
{
   for (int i = 0; i < global_symtab_count; i++)
   {
      if (strcmp(global_symtab[i].name, name) == 0)
         return global_symtab[i].address;
   }
   return 0; // Not found
}

void KMOD_PrintGlobalSymtab(void)
{
   logfmt(LOG_INFO, "\n[KMOD] ========== Global Symbol Table ==========");
   logfmt(LOG_INFO, "[KMOD] %-40s 0x%-8x %s", "Symbol", "Address", "Source");
   logfmt(LOG_INFO, "[KMOD] ==========================================\n");

   for (int i = 0; i < global_symtab_count; i++)
   {
      GlobalSymbolEntry *e = &global_symtab[i];
      const char *source = e->is_kernel ? "[KERNEL]" : e->lib_name;
      logfmt(LOG_INFO, "[KMOD] %-40s 0x%08x %s\n", e->name, e->address,
             source);
   }
   logfmt(LOG_INFO, "[KMOD] ==========================================\n");
   logfmt(LOG_INFO, "[KMOD] Total: %d symbols\n", global_symtab_count);
}

void KMOD_ClearGlobalSymtab(void)
{
   global_symtab_count = 0;
   logfmt(LOG_INFO, "[KMOD] Global symbol table cleared\n");
}

// ============================================================================
// Relocation Application
// ============================================================================

// Apply relocations to a loaded library or to the kernel
// Returns 0 on success, -1 on unresolved symbols
static int apply_relocations(uint32_t base, Elf32_Rel *rel_table,
                             uint32_t rel_count, uint32_t dynsym_addr,
                             uint32_t dynstr_addr, const char *context)
{
   if (!rel_table || rel_count == 0) return 0;

   for (uint32_t i = 0; i < rel_count; i++)
   {
      uint32_t r_offset =
          rel_table[i].r_offset; /* relocation target (usually absolute) */
      int type = ELF32_R_TYPE(rel_table[i].r_info);
      int symidx = ELF32_R_SYM(rel_table[i].r_info);

      /* Basic sanity checks before touching memory */
      if (r_offset == 0)
      {
         logfmt(LOG_ERROR,
                "[ERROR] Relocation[%d] has r_offset == 0 (skipping)\n", i);
         continue;
      }

      /* Verify target falls within expected area for this base. This avoids
       * writing to clearly invalid low addresses when the relocation entry
       * already contains an absolute virtual address. We allow a large
       * permitted range (1 MiB..+16 MiB) relative to base to be tolerant.
       */
      uint32_t allowed_low = base;
      uint32_t allowed_high = base + 0x0100000; /* 1 MiB window */
      if (r_offset < allowed_low || r_offset > allowed_high)
      {
         logfmt(LOG_ERROR,
                "[KMOD] Relocation[%d] target 0x%08x outside allowed range "
                "0x%08x-0x%08x\n",
                i, r_offset, allowed_low, allowed_high);
         return -1;
      }

      uint32_t *where = (uint32_t *)r_offset;
      uint32_t cur_val = *where;

      if (type == R_386_RELATIVE)
      {
         /* Relative relocation - the stored value at *where may already be
          * an absolute address (already relocated) or it may be an addend
          * that must be added to the runtime base. Avoid blindly adding
          * base to an already-correct absolute address (which produced
          * incorrect results and crashes).
          */
         uint32_t addend = cur_val;

         /* If the current value already falls inside the kernel image at
          * runtime, assume it was already relocated and skip rewriting it.
          */
         if (addend >= base && addend <= base + 0x00f00000)
         {
         }
         else if (addend < 0x01000000)
         {
            /* If addend is a small offset, treat it as an addend and
             * relocate to runtime base + addend.
             */
            uint32_t newv = base + addend;
            *where = newv;
         }
         else
         {
            logfmt(LOG_WARNING,
                   "[KMOD] R_386_RELATIVE at 0x%08x has unexpected value "
                   "0x%08x (skipping)\n",
                   r_offset, addend);
            continue;
         }
      }
      else if (type == R_386_32 || type == R_386_PC32 ||
               type == R_386_GLOB_DAT || type == R_386_JMP_SLOT)
      {
         if (symidx > 0 && dynsym_addr > 0)
         {
            uint32_t sym_ent_offset = symidx * 16;
            uint32_t st_name_offset =
                *(uint32_t *)(dynsym_addr + sym_ent_offset);

            if (dynstr_addr > 0)
            {
               const char *sym_name =
                   (const char *)(dynstr_addr + st_name_offset);

               uint32_t sym_addr = KMOD_LookupGlobalSymbol(sym_name);
               if (sym_addr == 0)
               {
                  logfmt(LOG_WARNING,
                         "[KMOD] Unresolved symbol in %s: %s (skipping "
                         "relocation)\n",
                         context, sym_name);
                  /* Don't abort the whole relocation pass for an unresolved
                   * symbol - warn and continue so other relocations (in
                   * particular .rel.plt entries) can still be applied. */
                  continue;
               }

               uint32_t addend = cur_val;

               switch (type)
               {
               case R_386_32:
               {
                  uint32_t newv = sym_addr + addend;
                  *where = newv;
               }
               break;
               case R_386_PC32:
               {
                  uint32_t newv = sym_addr + addend - (uint32_t)where;
                  *where = newv;
               }
               break;
               case R_386_GLOB_DAT:
               case R_386_JMP_SLOT:
               {
                  uint32_t newv = sym_addr;
                  *where = newv;
               }
               break;
               }
            }
         }
      }
   }

   return 0;
}

int KMOD_ApplyKernelRelocations(void)
{
   // Kernel relocation sections are exposed by linker script
   extern char _kernel_rel_dyn_start[];
   extern char _kernel_rel_dyn_end[];
   extern char _kernel_rel_plt_start[];
   extern char _kernel_rel_plt_end[];
   extern char _kernel_dynsym_start[];
   extern char _kernel_dynstr_start[];
   extern char __kernel_image_start[];

   uint32_t kernel_base = (uint32_t)(uintptr_t)__kernel_image_start;

   // Apply .rel.dyn relocations
   {
      uint32_t rel_size =
          (uint32_t)_kernel_rel_dyn_end - (uint32_t)_kernel_rel_dyn_start;
      Elf32_Rel *rel = (Elf32_Rel *)_kernel_rel_dyn_start;
      int rel_count = rel_size / sizeof(Elf32_Rel);

      if (rel_count > 0)
      {
         uint32_t dynsym_addr = (uint32_t)_kernel_dynsym_start;
         uint32_t dynstr_addr = (uint32_t)_kernel_dynstr_start;
         if (apply_relocations(kernel_base, rel, rel_count, dynsym_addr,
                               dynstr_addr, "kernel .rel.dyn") != 0)
            return -1;
      }
   }

   // Apply .rel.plt relocations
   {
      uint32_t rel_size =
          (uint32_t)_kernel_rel_plt_end - (uint32_t)_kernel_rel_plt_start;
      Elf32_Rel *rel = (Elf32_Rel *)_kernel_rel_plt_start;
      int rel_count = rel_size / sizeof(Elf32_Rel);

      if (rel_count > 0)
      {
         uint32_t dynsym_addr = (uint32_t)_kernel_dynsym_start;
         uint32_t dynstr_addr = (uint32_t)_kernel_dynstr_start;
         if (apply_relocations(kernel_base, rel, rel_count, dynsym_addr,
                               dynstr_addr, "kernel .rel.plt") != 0)
            return -1;

         /* Diagnostic: print GOT entries for JMP_SLOT relocations so we can
          * verify they point to the expected symbol addresses. */
         for (int ri = 0; ri < rel_count; ri++)
         {
            int rtype = ELF32_R_TYPE(rel[ri].r_info);
            int rsym = ELF32_R_SYM(rel[ri].r_info);
            if (rtype != R_386_JMP_SLOT) continue;

            uint32_t where_addr = rel[ri].r_offset; /* already absolute */
            uint32_t got_val = *(uint32_t *)where_addr;

            const char *sym_name = "(unknown)";
            uint32_t sym_addr = 0;
            if (dynsym_addr && dynstr_addr && rsym > 0)
            {
               uint32_t sym_ent_offset = rsym * 16;
               uint32_t st_name = *(uint32_t *)(dynsym_addr + sym_ent_offset);
               sym_name = (const char *)(dynstr_addr + st_name);
               sym_addr = *(uint32_t *)(dynsym_addr + sym_ent_offset + 4);
            }

            (void)got_val;
            (void)sym_name;
            (void)sym_addr;
         }
      }
   }
   return 0;
}

uint32_t KMOD_MemoryAllocate(const char *lib_name, uint32_t size)
{
   if (size == 0)
   {
      return 0;
   }

   if (!kmod_mem_initialized)
   {
      logfmt(LOG_INFO, "[KMOD] Initializing memory allocator...\n");
      if (KMOD_MemoryInitialize() != 0)
      {
         logfmt(LOG_ERROR, "[KMOD] Failed to initialize kmod memory\n");
         return 0;
      }
   }

   // Round up to 16-byte boundary for alignment
   uint32_t aligned_size = (size + 15) & ~15;

   void *alloc = kmalloc(aligned_size);
   if (!alloc)
   {
      logfmt(LOG_ERROR, "[KMOD] Out of heap memory allocating %u bytes\n",
             aligned_size);
      return 0;
   }

   int idx = find_index(lib_name);
   if (idx >= 0)
   {
      s_lib_registry[idx].base = alloc;
      extended_data[idx].alloc_size = aligned_size;
   }

   if (kmod_total_allocated <= UINT32_MAX - aligned_size)
      kmod_total_allocated += aligned_size;
   else
      kmod_total_allocated = UINT32_MAX;

   return (uint32_t)(uintptr_t)alloc;
}

// Helper: find index of library by name
static int find_index(const char *name)
{
   if (!name || name[0] == '\0') return -1;

   LibRecord *reg = s_lib_registry;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] != '\0')
      {
         if (str_eq(reg[i].name, name)) return i;
      }
   }
   return -1;
}

// Helper: create module record if it does not already exist
static int ensure_record(const char *name)
{
   if (!name || name[0] == '\0') return KMOD_EINVAL;

   int idx = find_index(name);
   if (idx >= 0) return idx;

   LibRecord *reg = s_lib_registry;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] != '\0')
      {
         continue;
      }

      memset(&reg[i], 0, sizeof(LibRecord));
      strncpy(reg[i].name, name, LIB_NAME_MAX - 1);
      reg[i].name[LIB_NAME_MAX - 1] = '\0';
      return i;
   }

   logfmt(LOG_ERROR, "[KMOD] module registry is full\n");
   return KMOD_ENOSLOT;
}

LibRecord *KMOD_Find(const char *name)
{
   if (!name)
   {
      logfmt(LOG_ERROR, "[KMOD] Invalid parameters to KMOD_Find\n");
      return NULL;
   }

   LibRecord *reg = s_lib_registry;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] != '\0')
      {
         if (str_eq(reg[i].name, name)) return &reg[i];
      }
   }
   return NULL;
}

int KMOD_CheckDependencies(const char *name)
{
   int idx = find_index(name);
   if (idx < 0) return 0;

   ExtendedLibData *ext = &extended_data[idx];

   // Check all dependencies
   for (int i = 0; i < ext->dep_count; i++)
   {
      if (!ext->deps[i].resolved)
      {
         logfmt(LOG_WARNING, "[KMOD] [UNRESOLVED] %s requires %s\n", name,
                ext->deps[i].name);
         return 0;
      }
   }
   return 1;
}

int KMOD_ResolveDependencies(const char *name)
{
   int idx = find_index(name);
   if (idx < 0) return -1;

   ExtendedLibData *ext = &extended_data[idx];

   // Resolve each dependency
   for (int i = 0; i < ext->dep_count; i++)
   {
      LibRecord *dep = KMOD_Find(ext->deps[i].name);
      if (dep)
      {
         ext->deps[i].resolved = 1;
         logfmt(LOG_INFO, "[KMOD] [OK] Found dependency: %s\n",
                ext->deps[i].name);
      }
      else
      {
         ext->deps[i].resolved = 0;
         logfmt(LOG_ERROR, "[KMOD] [ERROR] Missing dependency: %s\n",
                ext->deps[i].name);
         return -1;
      }
   }
   return 0;
}

int KMOD_CallIfExists(const char *name)
{
   LibRecord *lib = KMOD_Find(name);
   if (!lib || !lib->entry) return -1;

   // Check dependencies before calling
   if (!KMOD_CheckDependencies(name))
   {
      logfmt(LOG_ERROR, "[KMOD] %s has unresolved dependencies\n", name);
      return -1;
   }

   // Call the entry point
   typedef int (*entry_t)(void);
   return ((entry_t)lib->entry)();
}

void KMOD_List(void)
{
   LibRecord *reg = s_lib_registry;

   logfmt(LOG_INFO, "\n[KMOD] === Loaded Libraries ===\n");
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] == '\0') break;

      ExtendedLibData *ext = &extended_data[i];

      logfmt(LOG_INFO, "[KMOD] [%d] %s @ 0x%x\n", i, reg[i].name,
             (unsigned int)reg[i].entry);

      if (ext->dep_count > 0)
      {
         logfmt(LOG_INFO, "[KMOD]     Dependencies (%d):\n", ext->dep_count);
         for (int j = 0; j < ext->dep_count; j++)
         {
            char status = ext->deps[j].resolved ? '+' : '-';
            logfmt(LOG_INFO, "[KMOD]       [%c] %s\n", status,
                   ext->deps[j].name);
         }
      }
   }
   logfmt(LOG_INFO, "[KMOD]\n");
}

void KMOD_ListDependencies(const char *name)
{
   int idx = find_index(name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", name);
      return;
   }

   ExtendedLibData *ext = &extended_data[idx];

   logfmt(LOG_INFO, "[KMOD] Dependencies for %s:\n", name);
   if (ext->dep_count == 0)
   {
      logfmt(LOG_INFO, "[KMOD]   (none)\n");
      return;
   }

   for (int i = 0; i < ext->dep_count; i++)
   {
      const char *status = ext->deps[i].resolved ? "RESOLVED" : "UNRESOLVED";
      logfmt(LOG_INFO, "[KMOD]   %s: %s\n", ext->deps[i].name, status);
   }
   logfmt(LOG_INFO, "[KMOD]\n");
}

uint32_t KMOD_FindSymbol(const char *libname, const char *symname)
{
   int idx = find_index(libname);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", libname);
      return 0;
   }

   ExtendedLibData *ext = &extended_data[idx];

   // Search for symbol in library
   for (int i = 0; i < ext->symbol_count; i++)
   {
      if (str_eq(ext->symbols[i].name, symname))
      {
         return ext->symbols[i].address;
      }
   }

   logfmt(LOG_ERROR, "[KMOD] Symbol not found: %s::%s\n", libname, symname);
   return 0;
}

int KMOD_CallSymbol(const char *libname, const char *symname)
{
   LibRecord *lib = KMOD_Find(libname);
   if (!lib)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", libname);
      return -1;
   }

   // Check dependencies before calling
   if (!KMOD_CheckDependencies(libname))
   {
      logfmt(LOG_ERROR, "[KMOD] %s has unresolved dependencies\n", libname);
      return -1;
   }

   // Find the symbol
   uint32_t symbol_addr = KMOD_FindSymbol(libname, symname);
   if (!symbol_addr)
   {
      return -1;
   }

   // Call the symbol
   typedef int (*func_t)(void);
   return ((func_t)symbol_addr)();
}

void KMOD_ListSymbols(const char *name)
{
   int idx = find_index(name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", name);
      return;
   }

   ExtendedLibData *ext = &extended_data[idx];

   logfmt(LOG_INFO, "[KMOD] Exported symbols from %s:\n", name);
   if (ext->symbol_count == 0)
   {
      logfmt(LOG_INFO, "[KMOD]   (none)\n");
      return;
   }

   for (int i = 0; i < ext->symbol_count; i++)
   {
      logfmt(LOG_INFO, "[KMOD]   [%d] %s @ 0x%x\n", i, ext->symbols[i].name,
             ext->symbols[i].address);
   }
   logfmt(LOG_INFO, "[KMOD]\n");
}

int KMOD_ParseSymbols(LibRecord *lib)
{
   if (!lib || !lib->base)
   {
      logfmt(LOG_ERROR, "[KMOD] Invalid library record\n");
      return -1;
   }

   int idx = find_index(lib->name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found in registry: %s\n",
             lib->name);
      return -1;
   }

   ExtendedLibData *ext = &extended_data[idx];

   // Parse ELF symbols from the pre-loaded library at its base address
   logfmt(LOG_INFO,
          "[KMOD] Parsing symbols for pre-loaded library: %s at 0x%x\n",
          lib->name, (unsigned int)lib->base);

   parse_elf_symbols(ext, (uint32_t)lib->base, lib->size);

   ext->loaded = 1; // Mark as loaded so symbol table is available

   return 0;
}

int KMOD_MemoryFree(const char *lib_name)
{
   int idx = find_index(lib_name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", lib_name);
      return -1;
   }

   LibRecord *lib = &s_lib_registry[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (!lib->base || ext->alloc_size == 0)
   {
      logfmt(LOG_WARNING,
             "[KMOD] Library %s has no tracked heap allocation\n", lib_name);
      return 0;
   }

   uint32_t freed = ext->alloc_size;
   free(lib->base);
   lib->base = NULL;
   ext->alloc_size = 0;

   if (kmod_total_allocated >= freed)
      kmod_total_allocated -= freed;
   else
      kmod_total_allocated = 0;

   logfmt(LOG_INFO, "[KMOD] Freed 0x%x bytes for %s\n", freed, lib_name);

   return 0;
}

int KMOD_Load(const char *name, const void *image, uint32_t size)
{
   if (!kmod_mem_initialized) KMOD_MemoryInitialize();

   int idx = ensure_record(name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] failed to register module: %s\n", name);
      return -1;
   }

   LibRecord *lib = &s_lib_registry[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (ext->loaded)
   {
      logfmt(LOG_WARNING, "[KMOD] Library %s is already loaded\n", name);
      return -1;
   }

   // Allocate memory for the library
   uint32_t load_addr = KMOD_MemoryAllocate(name, size);
   if (!load_addr)
   {
      logfmt(LOG_ERROR, "[KMOD] Failed to allocate memory for %s\n", name);
      return -1;
   }

   // Copy library image to allocated memory
   void *dest = (void *)load_addr;
   const void *src = (const void *)image;
   memcpy(dest, src, size);

   // Update library record
   lib->base = (void *)load_addr;
   lib->size = size;
   ext->loaded = 1;

   logfmt(LOG_INFO, "[KMOD] Loaded %s (%d bytes) at 0x%x\n", name, size,
          load_addr);

   // Parse ELF symbols from the loaded library
   parse_elf_symbols(ext, load_addr, size);

   return 0;
}

// Parse ELF header and extract dynamic symbols from a loaded library
static int parse_elf_symbols(ExtendedLibData *ext, uint32_t base_addr,
                             uint32_t size)
{
   // Validate input parameters
   if (!ext || base_addr == 0 || size == 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Invalid parameters to KMOD_ParseELFSymbols\n");
      return -1;
   }

   // ELF header at the beginning of the loaded binary
   uint8_t *elf_data = (uint8_t *)base_addr;

   /* Basic bounds validation to avoid dereferencing beyond the provided
    * image. The minimal ELF32 header size we need is 52 bytes. If the size
    * passed in is smaller than that, treat the image as invalid. This
    * prevents accidental reads into unmapped memory which can cause kernel
    * crashes. */
   if (size < 52 || elf_data == NULL)
   {
      logfmt(LOG_ERROR,
             "[KMOD] ELF image too small or invalid (size=%d, data=%p)\n",
             size, elf_data);
      return -1;
   }

   /* Check ELF magic number */
   if (elf_data[0] != 0x7f || elf_data[1] != 'E' || elf_data[2] != 'L' ||
       elf_data[3] != 'F')
   {
      logfmt(LOG_ERROR, "[KMOD] Not a valid ELF file\n");
      return -1;
   }

   // Parse ELF32 header (little-endian)
   /* Validate we can read the header fields safely within the provided
    * image. Offsets 32, 46 and 48 are within the 52-byte header validated
    * above. */
   uint32_t e_shoff = *(uint32_t *)(elf_data + 32);
   uint16_t e_shnum = *(uint16_t *)(elf_data + 48);
   uint16_t e_shentsize = *(uint16_t *)(elf_data + 46);

   /* Quick sanity checks on section header metadata. Ensure the computed
    * section table fits inside the provided image. */
   if (e_shoff == 0 || e_shnum == 0 || e_shentsize == 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Invalid section headers\n");
      return 0;
   }

   /* Prevent overflow when computing total section headers area */
   uint64_t sh_table_size = (uint64_t)e_shnum * (uint64_t)e_shentsize;
   if (e_shoff + sh_table_size > size)
   {
      logfmt(LOG_ERROR, "[KMOD] Section header table out of bounds\n");
      return -1;
   }

   /* Find the first PROGBITS + SHF_ALLOC section (typically .text). Ensure
    * every access to section header fields is validated to lie within the
    * image. */
   uint32_t text_section_file_offset = 0;
   uint32_t text_section_vaddr = 0;
   for (int i = 0; i < e_shnum; i++)
   {
      uint64_t sh_off = e_shoff + (uint64_t)(i * e_shentsize);
      Elf32_Shdr *sh = (Elf32_Shdr *)(elf_data + sh_off);

      /* Ensure the section header itself fits inside the image */
      if (sh_off + sizeof(Elf32_Shdr) > size)
      {
         logfmt(LOG_ERROR, "[KMOD] Section header %d out of bounds\n", i);
         return -1;
      }

      if (sh->sh_type == 1 && (sh->sh_flags & 0x2)) /* PROGBITS + ALLOC */
      {
         /* Validate the referenced file offset is within the loaded image */
         if ((uint64_t)sh->sh_offset + sh->sh_size > size)
         {
            logfmt(LOG_ERROR, "[KMOD] .text section out of bounds\n");
            return -1;
         }
         text_section_file_offset = sh->sh_offset;
         text_section_vaddr = sh->sh_addr;
         break;
      }
   }

   // File offsets from base_addr need to be adjusted by the .text section
   // offset Memory layout: base_addr points to start of loaded file (including
   // ELF header) But symbols are addresses in the code section So:
   // symbol_memory_address = base_addr + file_offset_of_section +
   // offset_within_section
   //                           = base_addr + st_value_offset
   // where st_value_offset = st_value - original_base (offset from link
   // address)

   /* Read program header / entry fields. Offsets are within the standard
    * ELF header (validated above). */
   uint32_t e_entry = *(uint32_t *)(elf_data + 24);
   uint32_t e_phoff = *(uint32_t *)(elf_data + 28);
   uint16_t e_phentsize = *(uint16_t *)(elf_data + 42);
   uint16_t e_phnum = *(uint16_t *)(elf_data + 44);

   // Detect original_base from the ELF entry point (e_entry) which is an
   // absolute address in the linked image. For modules linked at a fixed base,
   // e_entry usually points into the first loadable text segment.
   uint32_t original_base =
       e_entry & 0xFFFF0000; // Mask to get base (assumes 64KB alignment)
   if (original_base == 0 && e_phoff != 0 && e_phnum != 0)
   {
      // Fallback: scan program headers for first PT_LOAD segment
      for (int i = 0; i < e_phnum; i++)
      {
         uint64_t ph_off = (uint64_t)e_phoff + (uint64_t)(i * e_phentsize);

         /* Ensure program header fits inside image */
         if (ph_off + e_phentsize > size)
         {
            continue;
         }

         uint8_t *ph = elf_data + ph_off;
         uint32_t p_type = *(uint32_t *)(ph + 0);
         uint32_t p_vaddr = *(uint32_t *)(ph + 8);
         if (p_type == 1)
         { // PT_LOAD
            original_base = p_vaddr & 0xFFFF0000;
            break;
         }
      }
   }
   if (original_base == 0)
   {
      if (text_section_vaddr >= text_section_file_offset)
      {
         original_base = text_section_vaddr - text_section_file_offset;
      }
      else
      {
            original_base = USER_CODE_START;  /* Fallback to user program base */
   for (int i = 0; i < e_shnum; i++)
   {
      Elf32_Shdr *sh = (Elf32_Shdr *)(elf_data + e_shoff + (i * e_shentsize));

      if (sh->sh_type == SHT_SYMTAB)
      {
         // Found symbol table - address is file offset + base (since we loaded
         // full file)
         symtab_addr = base_addr + sh->sh_offset;
         symtab_size = sh->sh_size;
         symtab_entsize = sh->sh_entsize;
         strtab_link = sh->sh_link; // Index of associated string table
      }
   }

   // Now find the associated string table
   if (strtab_link >= 0 && strtab_link < e_shnum)
   {
      Elf32_Shdr *sh =
          (Elf32_Shdr *)(elf_data + e_shoff + (strtab_link * e_shentsize));
      if (sh->sh_type == SHT_STRTAB)
      {
         strtab_addr = base_addr + sh->sh_offset;
         strtab_size = sh->sh_size;
      }
   }

   if (symtab_addr == 0 || strtab_addr == 0 || symtab_entsize == 0)
   {
      logfmt(
          LOG_ERROR,
          "[KMOD] Symbol table, string table, or entsize not found/invalid\n");
      return 0;
   }

   // Parse symbol entries
   uint32_t num_symbols = symtab_size / symtab_entsize;
   ext->symbol_count = 0;

   for (uint32_t i = 0;
        i < num_symbols && ext->symbol_count < KMOD_MAX_SYMBOLS; i++)
   {
      Elf32_Sym *sym = (Elf32_Sym *)(symtab_addr + (i * symtab_entsize));

      // Skip undefined and local symbols
      uint8_t st_bind = ELF32_ST_BIND(sym->st_info);

      if (st_bind == 0 || sym->st_shndx == 0)
         continue; // Skip local or undefined

      // Get symbol name from string table
      if (sym->st_name < strtab_size)
      {
         const char *sym_name = (const char *)(strtab_addr + sym->st_name);
         if (sym_name[0] != '\0')
         {
            // Add to symbol table
            strncpy(ext->symbols[ext->symbol_count].name, sym_name, 63);
            ext->symbols[ext->symbol_count].name[63] = '\0';

            // Symbol address calculation:
            // st_value is the absolute address in the linked image (e.g.,
            // 0x08000000 + offset) Offset from link base: st_value -
            // original_base Actual address: base_addr (ELF file start in
            // memory) + file_offset_of_section + offset_within_section But
            // st_value is already relative to 0x08000000, which is 0x1000 bytes
            // into the file (where .text starts) So: memory_addr = base_addr +
            // text_section_file_offset + (st_value - original_base)
            //               = base_addr + 0x1000 + (st_value - 0x08000000)
            uint32_t symbol_offset_in_code = sym->st_value - original_base;
            uint32_t symbol_addr =
                base_addr + text_section_file_offset + symbol_offset_in_code;
            ext->symbols[ext->symbol_count].address = symbol_addr;
            ext->symbol_count++;
         }
      }
   }

   logfmt(LOG_INFO, "[KMOD] Extracted %d symbols\n", ext->symbol_count);

   // NOTE: We previously had heuristic scanning that looked for embedded
   // addresses matching original_base and patched them. However, this caused
   // corruption of PIC code (position-independent code) which uses PC-relative
   // addressing via
   // __x86.get_pc_thunk and doesn't need runtime patching. The heuristic was
   // finding false positives in instruction immediates and corrupting code.
   //
   // PIC modules do not need heuristic address patching.
   // If we later need to support non-PIC libraries, we should use formal
   // relocation sections (SHT_REL) instead of heuristic scanning.

   for (int i = 0; i < e_shnum; i++)
   {
      Elf32_Shdr *sh = (Elf32_Shdr *)(elf_data + e_shoff + (i * e_shentsize));

      // Look for .rel.dyn or .rel.text sections (type 9 = SHT_REL)
      if (sh->sh_type == 9) // SHT_REL
      {
         uint32_t rel_addr = base_addr + sh->sh_offset;
         uint32_t rel_size = sh->sh_size;
         uint32_t rel_entsize = sh->sh_entsize;
         int rel_count = rel_size / rel_entsize;

         for (int j = 0; j < rel_count; j++)
         {
            Elf32_Rel *rel = (Elf32_Rel *)(rel_addr + (j * rel_entsize));
            uint32_t type = ELF32_R_TYPE(rel->r_info);

            /* r_offset in REL entries is typically a virtual address
             * relative to the linked base. Make sure we never write outside
             * the loaded image. */
            uint64_t target_addr =
                (uint64_t)base_addr + (uint64_t)rel->r_offset;
            uint32_t image_start = base_addr;
            uint32_t image_end = base_addr + size; // one past last byte

            if (target_addr < image_start || target_addr >= image_end)
            {
               continue;
            }

            uint32_t *patch_addr = (uint32_t *)(uint32_t)target_addr;

            // For R_386_RELATIVE, just add the difference between load and
            // original base
            if (type == R_386_RELATIVE)
            {
               uint32_t adjustment = base_addr - original_base;
               *patch_addr += adjustment;
            }
         }
      }
   }

   return 0;
}

int KMOD_LoadFromDisk(const char *name, const char *filepath)
{
   if (!kmod_mem_initialized) KMOD_MemoryInitialize();

   int idx = ensure_record(name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] failed to register module: %s\n", name);
      return -1;
   }

   LibRecord *lib = &s_lib_registry[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (ext->loaded)
   {
      logfmt(LOG_WARNING, "[KMOD] Library %s is already loaded\n", name);
      return -1;
   }

   // Open the library file from disk
   VFS_File *file = VFS_Open(filepath);
   if (!file)
   {
      logfmt(LOG_ERROR, "[KMOD] Failed to open file: %s\n", filepath);
      return -1;
   }

   // Get file size
   uint32_t file_size = VFS_GetSize(file);
   if (file_size == 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library file is empty: %s\n", filepath);
      VFS_Close(file);
      return -1;
   }

   // Allocate memory for the library
   uint32_t load_addr = KMOD_MemoryAllocate(name, file_size);
   if (!load_addr)
   {
      logfmt(LOG_ERROR,
             "[KMOD] Failed to allocate memory for %s (need %d bytes)\n", name,
             file_size);
      VFS_Close(file);
      return -1;
   }

   // Read library data from disk
   VFS_Seek(file, 0);
   uint32_t bytes_read = VFS_Read(file, file_size, (void *)load_addr);
   if (bytes_read != file_size)
   {
      logfmt(LOG_ERROR,
             "[KMOD] Failed to read library: expected %d bytes, got %d\n",
             file_size, bytes_read);
      VFS_Close(file);
      KMOD_MemoryFree(name);
      return -1;
   }

   // Close the file
   VFS_Close(file);

   // Update library record
   lib->base = (void *)load_addr;
   lib->size = file_size;
   ext->loaded = 1;

   logfmt(LOG_INFO, "[KMOD] Loaded %s (%d bytes) from disk at 0x%x\n", name,
          file_size, load_addr);

   // Parse ELF symbols from the loaded library
   parse_elf_symbols(ext, load_addr, file_size);

   if (symbol_callback)
   {
      symbol_callback(name);
   }

   return 0;
}

int KMOD_Remove(const char *name)
{
   int idx = find_index(name);
   if (idx < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] Library not found: %s\n", name);
      return -1;
   }

   LibRecord *lib = &s_lib_registry[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (!ext->loaded)
   {
      logfmt(LOG_WARNING, "[KMOD] Library %s is not loaded\n", name);
      return -1;
   }

   // Free memory
   if (KMOD_MemoryFree(name) != 0) return -1;

   // Mark as unloaded
   ext->loaded = 0;
   lib->base = NULL;
   lib->size = 0;

   // Clear dependency resolution
   for (int i = 0; i < ext->dep_count; i++)
   {
      ext->deps[i].resolved = 0;
   }

   logfmt(LOG_INFO, "[KMOD] Removed %s from memory\n", name);

   return 0;
}

void KMOD_MemoryStatus(void)
{
   if (!kmod_mem_initialized)
   {
      logfmt(LOG_ERROR, "[KMOD] Memory allocator not initialized\n");
      return;
   }

   uint32_t heap_start = (uint32_t)mem_heap_start();
   uint32_t heap_end = (uint32_t)mem_heap_end();
   uint32_t total_available =
       (heap_end >= heap_start) ? (heap_end - heap_start + 1) : 0;
   uint32_t total_allocated = kmod_total_allocated;
   uint32_t remaining =
       (total_available > total_allocated) ? (total_available - total_allocated)
                                           : 0;
   int percent_used =
       (total_available == 0)
           ? 0
           : (int)((total_allocated * 100) / total_available);

   logfmt(LOG_INFO, "[KMOD] === KMOD Memory Statistics ===");
   logfmt(LOG_INFO, "[KMOD] Total Memory:     %d MiB (0x%x - 0x%x)\n",
          total_available / 0x100000, heap_start, heap_end);
   logfmt(LOG_INFO, "[KMOD] Allocated:        %d KiB (%d%%)\n",
          total_allocated / 1024, percent_used);
   logfmt(LOG_INFO, "[KMOD] Available:        %d KiB\n", remaining / 1024);

   // List loaded libraries
   logfmt(LOG_INFO, "[KMOD] Loaded Libraries:\n");
   LibRecord *reg = s_lib_registry;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] == '\0') break;

      ExtendedLibData *ext = &extended_data[i];
      if (ext->loaded)
      {
         logfmt(LOG_INFO, "[KMOD]   %s: 0x%x bytes at 0x%x\n", reg[i].name,
                reg[i].size, (uint32_t)reg[i].base);
      }
   }
   logfmt(LOG_INFO, "[KMOD]\n");
}

void KMOD_RegisterCallback(kmod_register_symbols_t callback)
{
   symbol_callback = callback;
}

int KMOD_Initialize(void)
{
   if (KMOD_MemoryInitialize() < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] failed to initialize module memory\n");
      return KMOD_EINIT;
   }

   KMOD_ClearGlobalSymtab();

   if (KMOD_ApplyKernelRelocations() < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] failed to apply kernel relocations\n");
      return KMOD_EINIT;
   }

   return KMOD_OK;
}

int KMOD_Insmod(const char *name, const char *filepath)
{
   if (!name || name[0] == '\0' || !filepath || filepath[0] == '\0')
   {
      return KMOD_EINVAL;
   }

   if (KMOD_LoadFromDisk(name, filepath) < 0)
   {
      logfmt(LOG_ERROR, "[KMOD] insmod failed for %s from %s\n", name,
             filepath);
      return KMOD_ELOAD;
   }

   if (KMOD_ResolveDependencies(name) < 0)
   {
      KMOD_Remove(name);
      logfmt(LOG_ERROR, "[KMOD] unresolved dependencies for %s\n", name);
      return KMOD_EDEPEND;
   }

   LibRecord *lib = KMOD_Find(name);
   if (lib && lib->entry)
   {
      if (KMOD_CallIfExists(name) < 0)
      {
         KMOD_Remove(name);
         logfmt(LOG_ERROR, "[KMOD] module init failed for %s\n", name);
         return KMOD_EINITCALL;
      }
   }

   logfmt(LOG_INFO, "[KMOD] loaded module %s from %s\n", name, filepath);
   return KMOD_OK;
}

int KMOD_Rmmod(const char *name)
{
   if (!name || name[0] == '\0')
   {
      return KMOD_EINVAL;
   }

   if (KMOD_Remove(name) < 0)
   {
      return KMOD_ENOTFOUND;
   }

   logfmt(LOG_INFO, "[KMOD] removed module %s\n", name);
   return KMOD_OK;
}

int KMOD_IsLoaded(const char *name)
{
   int idx = find_index(name);
   if (idx < 0)
   {
      return 0;
   }
   return extended_data[idx].loaded ? 1 : 0;
}

void KMOD_Lsmod(void)
{
   LibRecord *reg = s_lib_registry;

   logfmt(LOG_INFO, "[KMOD] Module                  Size      State\n");
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] == '\0')
      {
         continue;
      }

      ExtendedLibData *ext = &extended_data[i];
      if (!ext->loaded)
      {
         continue;
      }

      logfmt(LOG_INFO, "[KMOD] %-22s 0x%08x loaded\n", reg[i].name,
             reg[i].size);
   }
}
