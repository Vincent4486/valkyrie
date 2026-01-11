// SPDX-License-Identifier: AGPL-3.0-or-later

/* Dynamic link helpers for the kernel. Supports true ELF dynamic linking
 * with PLT/GOT relocation. The bootloader (stage2) loads libraries and
 * populates a LibRecord table at LIB_REGISTRY_ADDR. The kernel loader
 * then applies relocations using the global symbol table.
 */

#include "dylib.h"
#include <fs/vfs/vfs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>
#include <stdint.h>
#include <sys/elf.h>

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
   DependencyRecord deps[DYLIB_MAX_DEPS];
   int dep_count;
   SymbolRecord symbols[DYLIB_MAX_SYMBOLS];
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

   int loaded; // 1 if loaded in memory, 0 if not
} ExtendedLibData;

// Memory allocator state
static int dylib_mem_initialized = 0;
static uint32_t dylib_mem_next_free = DYLIB_MEMORY_ADDR;
static ExtendedLibData extended_data[LIB_REGISTRY_MAX];

// Global symbol table - shared across all loaded libraries and kernel
static GlobalSymbolEntry global_symtab[DYLIB_MAX_GLOBAL_SYMBOLS];
static int global_symtab_count = 0;

// Forward declarations
static int parse_elf_symbols(ExtendedLibData *ext, uint32_t base_addr,
                             uint32_t size);

static dylib_register_symbols_t symbol_callback = NULL;

int Dylib_MemoryInitialize(void)
{
   if (dylib_mem_initialized) return 0;

   // Clear the memory region
   memset((void *)DYLIB_MEMORY_ADDR, 0, DYLIB_MEMORY_SIZE);

   // Clear extended data
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      extended_data[i].dep_count = 0;
      extended_data[i].symbol_count = 0;
      extended_data[i].dynsym_addr = 0;
      extended_data[i].dynstr_addr = 0;
      extended_data[i].rel_addr = 0;
      extended_data[i].jmprel_addr = 0;
      extended_data[i].loaded = 0;
   }

   dylib_mem_next_free = DYLIB_MEMORY_ADDR;
   dylib_mem_initialized = 1;

   logfmt(LOG_INFO, "[DYLIB] Memory allocator initialized: 0x%x - 0x%x (%d MiB)\n",
          DYLIB_MEMORY_ADDR, DYLIB_MEMORY_ADDR + DYLIB_MEMORY_SIZE,
          DYLIB_MEMORY_SIZE / 0x100000);

   return 0;
}

// ============================================================================
// Global Symbol Table Management
// ============================================================================

int Dylib_AddGlobalSymbol(const char *name, uint32_t address,
                          const char *lib_name, int is_kernel)
{
   if (global_symtab_count >= DYLIB_MAX_GLOBAL_SYMBOLS)
   {
      logfmt(LOG_ERROR, "[ERROR] Global symbol table full (%d entries)\n",
             DYLIB_MAX_GLOBAL_SYMBOLS);
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

uint32_t Dylib_LookupGlobalSymbol(const char *name)
{
   for (int i = 0; i < global_symtab_count; i++)
   {
      if (strcmp(global_symtab[i].name, name) == 0)
         return global_symtab[i].address;
   }
   return 0; // Not found
}

void Dylib_PrintGlobalSymtab(void)
{
   printf("\n========== Global Symbol Table ==========\n");
   printf("%-40s 0x%-8x %s\n", "Symbol", "Address", "Source");
   printf("==========================================\n");

   for (int i = 0; i < global_symtab_count; i++)
   {
      GlobalSymbolEntry *e = &global_symtab[i];
      const char *source = e->is_kernel ? "[KERNEL]" : e->lib_name;
      printf("%-40s 0x%08x %s\n", e->name, e->address, source);
   }
   printf("==========================================\n");
   printf("Total: %d symbols\n\n", global_symtab_count);
}

void Dylib_ClearGlobalSymtab(void)
{
   global_symtab_count = 0;
   logfmt(LOG_INFO, "[DYLIB] Global symbol table cleared\n");
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
         logfmt(LOG_ERROR, "[ERROR] Relocation[%d] has r_offset == 0 (skipping)\n", i);
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
         printf("[ERROR] Relocation[%d] target 0x%08x outside allowed range "
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
            printf("[WARNING] R_386_RELATIVE at 0x%08x has unexpected value "
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
            uint32_t st_value = *(uint32_t *)(dynsym_addr + sym_ent_offset + 4);

            if (dynstr_addr > 0)
            {
               const char *sym_name =
                   (const char *)(dynstr_addr + st_name_offset);

               uint32_t sym_addr = Dylib_LookupGlobalSymbol(sym_name);
               if (sym_addr == 0)
               {
                  printf("[WARNING] Unresolved symbol in %s: %s (skipping "
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

int Dylib_ApplyKernelRelocations(void)
{
   // Kernel relocation sections are exposed by linker script
   extern char _kernel_rel_dyn_start[];
   extern char _kernel_rel_dyn_end[];
   extern char _kernel_rel_plt_start[];
   extern char _kernel_rel_plt_end[];
   extern char _kernel_dynsym_start[];
   extern char _kernel_dynsym_end[];
   extern char _kernel_dynstr_start[];
   extern char _kernel_dynstr_end[];

   uint32_t kernel_base = 0x00A00000; // Kernel load address

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
         }
      }
   }
   return 0;
}

uint32_t Dylib_MemoryAllocate(const char *lib_name, uint32_t size)
{
   if (!dylib_mem_initialized) Dylib_MemoryInitialize();

   // Round up to 16-byte boundary for alignment
   uint32_t aligned_size = (size + 15) & ~15;

   // Check if we have enough space
   if (dylib_mem_next_free + aligned_size >
       DYLIB_MEMORY_ADDR + DYLIB_MEMORY_SIZE)
   {
      printf("[ERROR] Out of dylib memory! Need %d bytes, only %d available\n",
             aligned_size,
             DYLIB_MEMORY_ADDR + DYLIB_MEMORY_SIZE - dylib_mem_next_free);
      return 0;
   }

   uint32_t alloc_addr = dylib_mem_next_free;
   dylib_mem_next_free += aligned_size;

   return alloc_addr;
}

// Helper: find index of library by name
static int dylib_find_index(const char *name)
{
   LibRecord *reg = LIB_REGISTRY_ADDR;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] != '\0')
      {
         if (str_eq(reg[i].name, name)) return i;
      }
   }
   return -1;
}

LibRecord *Dylib_Find(const char *name)
{
   LibRecord *reg = LIB_REGISTRY_ADDR;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] != '\0')
      {
         if (str_eq(reg[i].name, name)) return &reg[i];
      }
   }
   return NULL;
}

int Dylib_CheckDependencies(const char *name)
{
   int idx = dylib_find_index(name);
   if (idx < 0) return 0;

   ExtendedLibData *ext = &extended_data[idx];

   // Check all dependencies
   for (int i = 0; i < ext->dep_count; i++)
   {
      if (!ext->deps[i].resolved)
      {
         printf("  [UNRESOLVED] %s requires %s\n", name, ext->deps[i].name);
         return 0;
      }
   }
   return 1;
}

int Dylib_ResolveDependencies(const char *name)
{
   int idx = dylib_find_index(name);
   if (idx < 0) return -1;

   ExtendedLibData *ext = &extended_data[idx];

   // Resolve each dependency
   for (int i = 0; i < ext->dep_count; i++)
   {
      LibRecord *dep = Dylib_Find(ext->deps[i].name);
      if (dep)
      {
         ext->deps[i].resolved = 1;
         printf("  [OK] Found dependency: %s\n", ext->deps[i].name);
      }
      else
      {
         ext->deps[i].resolved = 0;
         printf("  [ERROR] Missing dependency: %s\n", ext->deps[i].name);
         return -1;
      }
   }
   return 0;
}

int Dylib_CallIfExists(const char *name)
{
   LibRecord *lib = Dylib_Find(name);
   if (!lib || !lib->entry) return -1;

   // Check dependencies before calling
   if (!Dylib_CheckDependencies(name))
   {
      printf("[ERROR] %s has unresolved dependencies\n", name);
      return -1;
   }

   // Call the entry point
   typedef int (*entry_t)(void);
   return ((entry_t)lib->entry)();
}

void Dylib_List(void)
{
   LibRecord *reg = LIB_REGISTRY_ADDR;

   printf("\n=== Loaded Libraries ===\n");
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] == '\0') break;

      ExtendedLibData *ext = &extended_data[i];

      printf("[%d] %s @ 0x%x\n", i, reg[i].name, (unsigned int)reg[i].entry);

      if (ext->dep_count > 0)
      {
         printf("    Dependencies (%d):\n", ext->dep_count);
         for (int j = 0; j < ext->dep_count; j++)
         {
            char status = ext->deps[j].resolved ? '+' : '-';
            printf("      [%c] %s\n", status, ext->deps[j].name);
         }
      }
   }
   printf("\n");
}

void Dylib_ListDependencies(const char *name)
{
   int idx = dylib_find_index(name);
   if (idx < 0)
   {
      printf("[ERROR] Library not found: %s\n", name);
      return;
   }

   ExtendedLibData *ext = &extended_data[idx];

   printf("\nDependencies for %s:\n", name);
   if (ext->dep_count == 0)
   {
      printf("  (none)\n");
      return;
   }

   for (int i = 0; i < ext->dep_count; i++)
   {
      const char *status = ext->deps[i].resolved ? "RESOLVED" : "UNRESOLVED";
      printf("  %s: %s\n", ext->deps[i].name, status);
   }
   printf("\n");
}

uint32_t Dylib_FindSymbol(const char *libname, const char *symname)
{
   int idx = dylib_find_index(libname);
   if (idx < 0)
   {
      printf("[ERROR] Library not found: %s\n", libname);
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

   printf("[ERROR] Symbol not found: %s::%s\n", libname, symname);
   return 0;
}

int Dylib_CallSymbol(const char *libname, const char *symname)
{
   LibRecord *lib = Dylib_Find(libname);
   if (!lib)
   {
      printf("[ERROR] Library not found: %s\n", libname);
      return -1;
   }

   // Check dependencies before calling
   if (!Dylib_CheckDependencies(libname))
   {
      printf("[ERROR] %s has unresolved dependencies\n", libname);
      return -1;
   }

   // Find the symbol
   uint32_t symbol_addr = Dylib_FindSymbol(libname, symname);
   if (!symbol_addr)
   {
      return -1;
   }

   // Call the symbol
   typedef int (*func_t)(void);
   return ((func_t)symbol_addr)();
}

void Dylib_ListSymbols(const char *name)
{
   int idx = dylib_find_index(name);
   if (idx < 0)
   {
      printf("[ERROR] Library not found: %s\n", name);
      return;
   }

   ExtendedLibData *ext = &extended_data[idx];

   printf("\nExported symbols from %s:\n", name);
   if (ext->symbol_count == 0)
   {
      printf("  (none)\n");
      return;
   }

   for (int i = 0; i < ext->symbol_count; i++)
   {
      printf("  [%d] %s @ 0x%x\n", i, ext->symbols[i].name,
             ext->symbols[i].address);
   }
   printf("\n");
}

int Dylib_ParseSymbols(LibRecord *lib)
{
   if (!lib || !lib->base)
   {
      printf("[ERROR] Invalid library record\n");
      return -1;
   }

   int idx = dylib_find_index(lib->name);
   if (idx < 0)
   {
      printf("[ERROR] Library not found in registry: %s\n", lib->name);
      return -1;
   }

   ExtendedLibData *ext = &extended_data[idx];

   // Parse ELF symbols from the pre-loaded library at its base address
   logfmt(LOG_INFO, "[DYLIB] Parsing symbols for pre-loaded library: %s at 0x%x\n",
          lib->name, (unsigned int)lib->base);

   parse_elf_symbols(ext, (uint32_t)lib->base, lib->size);

   ext->loaded = 1; // Mark as loaded so symbol table is available

   return 0;
}

int Dylib_MemoryFree(const char *lib_name)
{
   int idx = dylib_find_index(lib_name);
   if (idx < 0)
   {
      printf("[ERROR] Library not found: %s\n", lib_name);
      return -1;
   }

   LibRecord *lib = &LIB_REGISTRY_ADDR[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (!ext->loaded)
   {
      printf("[WARNING] Library %s is not loaded\n", lib_name);
      return -1;
   }

   // Note: We don't actually free the memory in the pool since it's a linear
   // allocator Just mark as unloaded
   logfmt(LOG_INFO, "[DYLIB] Freed 0x%x bytes for %s\n", lib->size, lib_name);

   return 0;
}

int Dylib_Load(const char *name, const void *image, uint32_t size)
{
   if (!dylib_mem_initialized) Dylib_MemoryInitialize();

   int idx = dylib_find_index(name);
   if (idx < 0)
   {
      printf("[ERROR] Library record not found: %s\n", name);
      return -1;
   }

   LibRecord *lib = &LIB_REGISTRY_ADDR[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (ext->loaded)
   {
      printf("[WARNING] Library %s is already loaded\n", name);
      return -1;
   }

   // Allocate memory for the library
   uint32_t load_addr = Dylib_MemoryAllocate(name, size);
   if (!load_addr)
   {
      printf("[ERROR] Failed to allocate memory for %s\n", name);
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

   logfmt(LOG_INFO, "[DYLIB] Loaded %s (%d bytes) at 0x%x\n", name, size, load_addr);

   // Parse ELF symbols from the loaded library
   parse_elf_symbols(ext, load_addr, size);

   return 0;
}

// Parse ELF header and extract dynamic symbols from a loaded library
static int parse_elf_symbols(ExtendedLibData *ext, uint32_t base_addr,
                             uint32_t size)
{
   // ELF header at the beginning of the loaded binary
   uint8_t *elf_data = (uint8_t *)base_addr;

   // Check ELF magic number
   if (elf_data[0] != 0x7f || elf_data[1] != 'E' || elf_data[2] != 'L' ||
       elf_data[3] != 'F')
   {
      printf("[ERROR] Not a valid ELF file\n");
      return -1;
   }

   // Parse ELF32 header (little-endian)
   uint32_t e_shoff =
       *(uint32_t *)(elf_data + 32); // Section header offset (in file)
   uint16_t e_shnum = *(uint16_t *)(elf_data + 48); // Number of sections
   uint16_t e_shentsize =
       *(uint16_t *)(elf_data + 46); // Section header entry size

   if (e_shoff == 0 || e_shnum == 0 || e_shentsize == 0)
   {
      logfmt(LOG_ERROR, "[DYLIB] Invalid section headers\n");
      return 0;
   }

   // Find the first PROGBITS section to determine the offset adjustment
   // When we load the ELF file, all sections keep their relative offsets
   // But the sections that need to be in memory are those with SHF_ALLOC flag
   // We need to find where .text actually starts in the file
   uint32_t text_section_file_offset = 0;
   for (int i = 0; i < e_shnum; i++)
   {
      Elf32_Shdr *sh = (Elf32_Shdr *)(elf_data + e_shoff + (i * e_shentsize));
      // Find the first allocable section - this is where code actually starts
      if (sh->sh_type == 1 && (sh->sh_flags & 0x2)) // PROGBITS with ALLOC
      {
         text_section_file_offset = sh->sh_offset;
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

   // Read ELF header fields for detecting original_base
   uint32_t e_entry = *(uint32_t *)(elf_data + 24); // Entry point address
   uint32_t e_phoff = *(uint32_t *)(elf_data + 28); // Program header offset
   uint16_t e_phentsize =
       *(uint16_t *)(elf_data + 42);                // Program header entry size
   uint16_t e_phnum = *(uint16_t *)(elf_data + 44); // Number of program headers

   // Detect original_base from the ELF entry point (e_entry) which is an
   // absolute address in the linked image. For libmath linked at 0x05000000,
   // e_entry will be something like 0x05001000. We can mask off the low bits to
   // get the base.
   uint32_t original_base =
       e_entry & 0xFFFF0000; // Mask to get base (assumes 64KB alignment)
   if (original_base == 0 && e_phoff != 0 && e_phnum != 0)
   {
      // Fallback: scan program headers for first PT_LOAD segment
      for (int i = 0; i < e_phnum; i++)
      {
         uint8_t *ph = elf_data + e_phoff + (i * e_phentsize);
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
      original_base = 0x05000000; // Default for our libmath
   }
   logfmt(LOG_INFO, "[DYLIB] Detected original_base = 0x%x (from e_entry=0x%x)\n",
          original_base, e_entry);

   // Find .symtab and .strtab sections
   uint32_t symtab_addr = 0, symtab_size = 0, symtab_entsize = 0;
   uint32_t strtab_addr = 0, strtab_size = 0;
   int strtab_link = -1;

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
         logfmt(LOG_INFO, "[DYLIB] Found .symtab at file offset 0x%x, memory 0x%x, "
                "size=%d, entsize=%d, strtab_link=%d\n",
                sh->sh_offset, symtab_addr, symtab_size, symtab_entsize,
                strtab_link);
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
      printf(
          "[DYLIB] Symbol table, string table, or entsize not found/invalid\n");
      return 0;
   }

   // Parse symbol entries
   uint32_t num_symbols = symtab_size / symtab_entsize;
   ext->symbol_count = 0;

   for (uint32_t i = 0;
        i < num_symbols && ext->symbol_count < DYLIB_MAX_SYMBOLS; i++)
   {
      Elf32_Sym *sym = (Elf32_Sym *)(symtab_addr + (i * symtab_entsize));

      // Skip undefined and local symbols
      uint8_t st_bind = ELF32_ST_BIND(sym->st_info);
      uint8_t st_type = ELF32_ST_TYPE(sym->st_info);

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

   logfmt(LOG_INFO, "[DYLIB] Extracted %d symbols\n", ext->symbol_count);

   // NOTE: We previously had heuristic scanning that looked for embedded
   // addresses matching original_base and patched them. However, this caused
   // corruption of PIC code (position-independent code) which uses PC-relative
   // addressing via
   // __x86.get_pc_thunk and doesn't need runtime patching. The heuristic was
   // finding false positives in instruction immediates and corrupting code.
   //
   // Since libmath is built with -fPIC, it doesn't need address patching.
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

         logfmt(LOG_INFO, "[DYLIB]   Applying %d relocations from section %d\n",
                rel_count, i);

         for (int j = 0; j < rel_count; j++)
         {
            Elf32_Rel *rel = (Elf32_Rel *)(rel_addr + (j * rel_entsize));
            uint32_t *patch_addr = (uint32_t *)(base_addr + rel->r_offset);
            uint32_t type = ELF32_R_TYPE(rel->r_info);

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

int Dylib_LoadFromDisk(const char *name, const char *filepath)
{
   if (!dylib_mem_initialized) Dylib_MemoryInitialize();

   int idx = dylib_find_index(name);
   if (idx < 0)
   {
      printf("[ERROR] Library record not found: %s\n", name);
      return -1;
   }

   LibRecord *lib = &LIB_REGISTRY_ADDR[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (ext->loaded)
   {
      printf("[WARNING] Library %s is already loaded\n", name);
      return -1;
   }

   // Open the library file from disk
   logfmt(LOG_INFO, "[DYLIB] Opening %s from disk...\n", filepath);
   VFS_File *file = VFS_Open(filepath);
   if (!file)
   {
      printf("[ERROR] Failed to open file: %s\n", filepath);
      return -1;
   }

   // Get file size
   uint32_t file_size = VFS_GetSize(file);
   if (file_size == 0)
   {
      printf("[ERROR] Library file is empty: %s\n", filepath);
      VFS_Close(file);
      return -1;
   }

   // Allocate memory for the library
   uint32_t load_addr = Dylib_MemoryAllocate(name, file_size);
   if (!load_addr)
   {
      printf("[ERROR] Failed to allocate memory for %s (need %d bytes)\n", name,
             file_size);
      VFS_Close(file);
      return -1;
   }

   // Read library data from disk
   VFS_Seek(file, 0);
   uint32_t bytes_read = VFS_Read(file, file_size, (void *)load_addr);
   if (bytes_read != file_size)
   {
      printf("[ERROR] Failed to read library: expected %d bytes, got %d\n",
             file_size, bytes_read);
      VFS_Close(file);
      Dylib_MemoryFree(name);
      return -1;
   }

   // Close the file
   VFS_Close(file);

   // Update library record
   lib->base = (void *)load_addr;
   lib->size = file_size;
   ext->loaded = 1;

   logfmt(LOG_INFO, "[DYLIB] Loaded %s (%d bytes) from disk at 0x%x\n", name, file_size,
          load_addr);

   // Parse ELF symbols from the loaded library
   parse_elf_symbols(ext, load_addr, file_size);

   if (symbol_callback)
   {
      symbol_callback(name);
   }

   return 0;
}

int Dylib_Remove(const char *name)
{
   int idx = dylib_find_index(name);
   if (idx < 0)
   {
      printf("[ERROR] Library not found: %s\n", name);
      return -1;
   }

   LibRecord *lib = &LIB_REGISTRY_ADDR[idx];
   ExtendedLibData *ext = &extended_data[idx];

   if (!ext->loaded)
   {
      printf("[WARNING] Library %s is not loaded\n", name);
      return -1;
   }

   // Free memory
   if (Dylib_MemoryFree(name) != 0) return -1;

   // Mark as unloaded
   ext->loaded = 0;
   lib->base = NULL;
   lib->size = 0;

   // Clear dependency resolution
   for (int i = 0; i < ext->dep_count; i++)
   {
      ext->deps[i].resolved = 0;
   }

   logfmt(LOG_INFO, "[DYLIB] Removed %s from memory\n", name);

   return 0;
}

void Dylib_MemoryStatus(void)
{
   if (!dylib_mem_initialized)
   {
      logfmt(LOG_ERROR, "[DYLIB] Memory allocator not initialized\n");
      return;
   }

   uint32_t total_allocated = dylib_mem_next_free - DYLIB_MEMORY_ADDR;
   uint32_t total_available = DYLIB_MEMORY_SIZE;
   uint32_t remaining = total_available - total_allocated;
   int percent_used = (total_allocated * 100) / total_available;

   printf("\n=== Dylib Memory Statistics ===\n");
   printf("Total Memory:     %d MiB (0x%x - 0x%x)\n",
          total_available / 0x100000, DYLIB_MEMORY_ADDR,
          DYLIB_MEMORY_ADDR + DYLIB_MEMORY_SIZE);
   printf("Allocated:        %d KiB (%d%%)\n", total_allocated / 1024,
          percent_used);
   printf("Available:        %d KiB\n", remaining / 1024);

   // List loaded libraries
   printf("\nLoaded Libraries:\n");
   LibRecord *reg = (LibRecord *)LIB_REGISTRY_ADDR;
   for (int i = 0; i < LIB_REGISTRY_MAX; i++)
   {
      if (reg[i].name[0] == '\0') break;

      ExtendedLibData *ext = &extended_data[i];
      if (ext->loaded)
      {
         printf("  %s: 0x%x bytes at 0x%x\n", reg[i].name, reg[i].size,
                (uint32_t)reg[i].base);
      }
   }
   printf("\n");
}

void Dylib_RegisterCallback(dylib_register_symbols_t callback)
{
   symbol_callback = callback;
}

#include <fs/fs.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <stddef.h>
#include <sys/dylib.h>

static int load_libmath(void)
{
   // First, ensure libmath is registered in the library registry
   LibRecord *lib_registry = LIB_REGISTRY_ADDR;

   // Check if libmath is already registered
   LibRecord *existing_lib = Dylib_Find("libmath");
   if (!existing_lib || existing_lib->name[0] == '\0')
   {
      // Register libmath in the registry
      lib_registry[0].name[0] = 'l';
      lib_registry[0].name[1] = 'i';
      lib_registry[0].name[2] = 'b';
      lib_registry[0].name[3] = 'm';
      lib_registry[0].name[4] = 'a';
      lib_registry[0].name[5] = 't';
      lib_registry[0].name[6] = 'h';
      lib_registry[0].name[7] = '\0';
      lib_registry[0].base = NULL;
      lib_registry[0].entry = NULL;
      lib_registry[0].size = 0;
   }

   // Load libmath from disk using the standard loader
   if (Dylib_LoadFromDisk("libmath", "/usr/lib/libmath.so") != 0)
   {
      printf("[ERROR] Failed to load libmath.so\n");
      return -1;
   }

   // Resolve dependencies
   Dylib_ResolveDependencies("libmath");

   // Dylib_ListSymbols("libmath");

   // Register libmath symbols in global symbol table for GOT patching
   Dylib_AddGlobalSymbol("add", (uint32_t)Dylib_FindSymbol("libmath", "add"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("subtract",
                         (uint32_t)Dylib_FindSymbol("libmath", "subtract"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("multiply",
                         (uint32_t)Dylib_FindSymbol("libmath", "multiply"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "divide", (uint32_t)Dylib_FindSymbol("libmath", "divide"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "modulo", (uint32_t)Dylib_FindSymbol("libmath", "modulo"), "libmath", 0);
   Dylib_AddGlobalSymbol("abs_int",
                         (uint32_t)Dylib_FindSymbol("libmath", "abs_int"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "fabsf", (uint32_t)Dylib_FindSymbol("libmath", "fabsf"), "libmath", 0);
   Dylib_AddGlobalSymbol("fabs", (uint32_t)Dylib_FindSymbol("libmath", "fabs"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("sinf", (uint32_t)Dylib_FindSymbol("libmath", "sinf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("sin", (uint32_t)Dylib_FindSymbol("libmath", "sin"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("cosf", (uint32_t)Dylib_FindSymbol("libmath", "cosf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("cos", (uint32_t)Dylib_FindSymbol("libmath", "cos"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("tanf", (uint32_t)Dylib_FindSymbol("libmath", "tanf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("tan", (uint32_t)Dylib_FindSymbol("libmath", "tan"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("expf", (uint32_t)Dylib_FindSymbol("libmath", "expf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("exp", (uint32_t)Dylib_FindSymbol("libmath", "exp"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("logf", (uint32_t)Dylib_FindSymbol("libmath", "logf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("log", (uint32_t)Dylib_FindSymbol("libmath", "log"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "log10f", (uint32_t)Dylib_FindSymbol("libmath", "log10f"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "log10", (uint32_t)Dylib_FindSymbol("libmath", "log10"), "libmath", 0);
   Dylib_AddGlobalSymbol("powf", (uint32_t)Dylib_FindSymbol("libmath", "powf"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol("pow", (uint32_t)Dylib_FindSymbol("libmath", "pow"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "sqrtf", (uint32_t)Dylib_FindSymbol("libmath", "sqrtf"), "libmath", 0);
   Dylib_AddGlobalSymbol("sqrt", (uint32_t)Dylib_FindSymbol("libmath", "sqrt"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "floorf", (uint32_t)Dylib_FindSymbol("libmath", "floorf"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "floor", (uint32_t)Dylib_FindSymbol("libmath", "floor"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "ceilf", (uint32_t)Dylib_FindSymbol("libmath", "ceilf"), "libmath", 0);
   Dylib_AddGlobalSymbol("ceil", (uint32_t)Dylib_FindSymbol("libmath", "ceil"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "roundf", (uint32_t)Dylib_FindSymbol("libmath", "roundf"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "round", (uint32_t)Dylib_FindSymbol("libmath", "round"), "libmath", 0);
   Dylib_AddGlobalSymbol(
       "fminf", (uint32_t)Dylib_FindSymbol("libmath", "fminf"), "libmath", 0);
   Dylib_AddGlobalSymbol("fmin", (uint32_t)Dylib_FindSymbol("libmath", "fmin"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "fmaxf", (uint32_t)Dylib_FindSymbol("libmath", "fmaxf"), "libmath", 0);
   Dylib_AddGlobalSymbol("fmax", (uint32_t)Dylib_FindSymbol("libmath", "fmax"),
                         "libmath", 0);
   Dylib_AddGlobalSymbol(
       "fmodf", (uint32_t)Dylib_FindSymbol("libmath", "fmodf"), "libmath", 0);
   Dylib_AddGlobalSymbol("fmod", (uint32_t)Dylib_FindSymbol("libmath", "fmod"),
                         "libmath", 0);

   Dylib_ApplyKernelRelocations();
   return 0;
}

bool Dylib_Initialize(void)
{
   // Load math library
   if (load_libmath() != 0)
   {
      printf("[ERROR] Failed to initialize libmath\n");
      return false;
   }

   return true;
}
