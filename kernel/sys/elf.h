// SPDX-License-Identifier: AGPL-3.0-or-later

// Simple ELF32 loader for stage2 bootloader
#ifndef ELF_H
#define ELF_H

#include <cpu/process.h>
#include <stdbool.h>
#include <stdint.h>
#include <valkyrie/fs.h>

// ELF structures for 32-bit little-endian
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

// Load an ELF32 file from an opened VFS_File into memory. On success returns
// true and sets *entryOut to the ELF entry point (as a pointer). The loader
// will read program headers (PT_LOAD) and copy them to their p_paddr (or
// p_vaddr if p_paddr is zero), zeroing the BSS area when necessary.
bool ELF_Load(VFS_File *file, void **entryOut);

// Load an ELF32 executable file into a new process's isolated address space.
// Opens the file by name through VFS, parses the ELF header, allocates
// pages in the process's page directory, and copies segments. Returns the new
// Process on success, or NULL on failure.
Process *ELF_LoadProcess(const char *filename, bool kernel_mode);

#endif