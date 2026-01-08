// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef SYS_H
#define SYS_H
#include <fs/disk/disk.h>
#include <fs/fs.h>
#include <hal/irq.h>
#include <mem/mm_kernel.h>
#include <stdint.h>
#include <valkyrie/system.h>

extern __attribute__((cdecl)) void get_arch(uint8_t *arch);
extern __attribute__((cdecl)) void get_cpu_count(uint32_t *cpu_count);
extern __attribute__((cdecl)) void get_cpu_brand(char *brand);
extern __attribute__((cdecl)) uint32_t get_cpu_frequency(void);
extern __attribute__((cdecl)) uint32_t get_cache_line_size(void);
extern __attribute__((cdecl)) uint32_t get_cpu_features(void);

/* Multiboot structures for memory detection */
typedef struct
{
   uint32_t flags;
   uint32_t mem_lower;
   uint32_t mem_upper;
   uint32_t boot_device;
   uint32_t cmdline;
   uint32_t mods_count;
   uint32_t mods_addr;
   uint32_t syms[4];
   uint32_t mmap_length;
   uint32_t mmap_addr;
} __attribute__((packed)) multiboot_info_t;

typedef struct
{
   uint32_t size;
   uint64_t base_addr;
   uint64_t length;
   uint32_t type; /* 1 = available RAM */
} __attribute__((packed)) multiboot_mmap_entry_t;

/* Architecture/CPU information */
typedef struct
{
   uint8_t arch;             /* Architecture (1: i686, 2: x86_64, 3: aarch64) */
   uint32_t cpu_count;       /* Number of CPUs/cores */
   uint32_t cpu_frequency;   /* CPU frequency in MHz */
   uint32_t cache_line_size; /* L1 cache line size */
   uint32_t features;        /* CPU feature flags (MMU, PAE, etc) */
   char cpu_brand[64];       /* CPU brand string */
} ARCH_Info;

/* Master system information structure */
typedef struct
{
   /* Kernel version and identification */
   uint16_t kernel_major;   /* Kernel major version */
   uint16_t kernel_minor;   /* Kernel minor version */
   uint64_t uptime_seconds; /* Uptime in seconds */

   /* Architecture and CPU */
   ARCH_Info arch; /* Architecture information */

   /* Memory */
   MEM_Info memory; /* Memory information */

   /* Storage */
   Partition volume[MAX_DISKS]; /* Primary disk information */
   uint8_t disk_count;          /* Number of disk devices */

   /* Interrupts */
   IRQ_Info irq; /* Interrupt controller information */

   /* Bootloader and hardware */
   uint32_t boot_device; /* Device booted from */
   uint32_t cmdline;
   uint32_t video_memory; /* Video memory size in bytes */
   uint16_t video_width;  /* Video width in pixels/chars */
   uint16_t video_height; /* Video height in pixels/chars */

   /* Status flags */
   uint8_t initialized; /* 1 if fully initialized, 0 otherwise */
   uint8_t reserved[3]; /* Padding for alignment */
} SYS_Info;

/* Global system info pointer (defined in sys.c) */
extern SYS_Info *g_SysInfo;

void SYS_Initialize();
void SYS_Finalize();

#endif
