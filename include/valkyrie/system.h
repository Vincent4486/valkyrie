// SPDX-License-Identifier: GPL-3.0-only

#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

#define MAX_DISKS 32

/**
 * BOOT_MemMapEntry - One entry from the bootloader's physical memory map.
 *
 * Layout mirrors the Multiboot v1 mmap entry so the arch parser can cast
 * directly; the rest of the kernel only sees this kernel-owned type.
 *
 * `size` is the byte length of the *remaining* fields (base_addr … type),
 * i.e. the entry stride is (size + sizeof(size)).
 */
typedef struct
{
   uint32_t size;      /* Bytes that follow this field in the entry */
   uint64_t baseAddr;  /* First physical byte of the region */
   uint64_t length;    /* Byte length of the region */
   uint32_t type;      /* 1 = available RAM; anything else = reserved/unusable */
} __attribute__((packed)) BOOT_MemMapEntry;

/**
 * BOOT_Info - Bootloader-agnostic boot parameters.
 *
 * Populated by Parser_Multiboot (kernel/arch/i686/boot/parser.c) before
 * kernel_main / start() is invoked.  No raw Multiboot types are exposed
 * beyond kernel/arch/i686/boot/.
 */
typedef struct
{
   char     commandLine[256];   /* Null-terminated kernel command line */
   uint32_t memMapAddr;         /* Physical address of the memory map table */
   uint32_t memMapLength;       /* Byte length of the memory map table */
   char     bootLoaderName[64]; /* Null-terminated bootloader name string */
   uint32_t totalMemoryUpper;   /* Memory above 1 MB reported by bootloader (KB) */
} BOOT_Info;

#endif