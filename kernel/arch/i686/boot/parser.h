// SPDX-License-Identifier: GPL-3.0-only
/*
 * kernel/arch/i686/boot/parser.h
 *
 * Multiboot v1 structures and the pre-kernel parser declaration.
 *
 * RAW MULTIBOOT TYPES ARE INTENTIONALLY CONFINED TO THIS FILE AND parser.c.
 * No other kernel subsystem should ever include this header directly.
 */

#ifndef BOOT_PARSER_H
#define BOOT_PARSER_H

#include <stdint.h>
#include <valkyrie/system.h>

/* -------------------------------------------------------------------------
 * Multiboot v1 information structure (Multiboot spec §3.3)
 * Passed by the bootloader in %ebx; physical address, read-only.
 * ------------------------------------------------------------------------- */
typedef struct
{
   uint32_t flags;            /* Bitmask of valid fields */
   uint32_t mem_lower;        /* KB of memory below 1 MB */
   uint32_t mem_upper;        /* KB of memory above 1 MB */
   uint32_t boot_device;      /* BIOS boot device */
   uint32_t cmdline;          /* Physical address of command-line string */
   uint32_t mods_count;       /* Number of boot modules */
   uint32_t mods_addr;        /* Physical address of modules array */
   uint32_t syms[4];          /* Symbol table info (ELF / a.out) */
   uint32_t mmap_length;      /* Byte length of memory-map buffer */
   uint32_t mmap_addr;        /* Physical address of memory-map buffer */
   uint32_t drives_length;    /* Byte length of drive info buffer */
   uint32_t drives_addr;      /* Physical address of drive info buffer */
   uint32_t config_table;     /* Physical address of ROM configuration table */
   uint32_t boot_loader_name; /* Physical address of bootloader name string */
} __attribute__((packed)) multiboot_info_t;

/* -------------------------------------------------------------------------
 * Multiboot v1 memory-map entry (Multiboot spec §3.3)
 * ------------------------------------------------------------------------- */
typedef struct
{
   uint32_t size;      /* Size of this entry (not counting this field) */
   uint64_t base_addr; /* Starting physical address of the region */
   uint64_t length;    /* Byte length of the region */
   uint32_t type;      /* 1 = available RAM; anything else = reserved */
} __attribute__((packed)) multiboot_mmap_entry_t;

/* -------------------------------------------------------------------------
 * Magic value placed in %eax by a Multiboot-compliant bootloader (GRUB).
 * ------------------------------------------------------------------------- */
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002U

/* -------------------------------------------------------------------------
 * Parser entry-point.
 *
 * Called from entry.S immediately after the processor is in a known state.
 * Translates the raw Multiboot structures into a BOOT_Info and then calls
 * void start(BOOT_Info *boot).
 *
 * Parameters (cdecl, matching the entry.S push order):
 *   magic  – value of %eax at kernel entry (must equal
 * MULTIBOOT_BOOTLOADER_MAGIC) mbi    – physical pointer to the multiboot_info_t
 * structure (%ebx at entry)
 * ------------------------------------------------------------------------- */
void Parser_Multiboot(uint32_t magic, multiboot_info_t *mbi);

#endif /* BOOT_PARSER_H */
