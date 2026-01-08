// SPDX-License-Identifier: AGPL-3.0-or-later

#include <valkyrie/valkyrie.h>

#include "sys.h"
#include <mem/mm_kernel.h>
#include <std/stdio.h>
#include <std/string.h>

/* Global SYS_Info structure (allocated in SYS_Initialize) */
SYS_Info *g_SysInfo = (SYS_Info *)SYS_INFO_ADDR;

void SYS_Initialize()
{
   /* Initialize SYS_Info structure */
   g_SysInfo->kernel_major = KERNEL_MAJOR;
   g_SysInfo->kernel_minor = KERNEL_MINOR;
   g_SysInfo->uptime_seconds = 0;
   g_SysInfo->initialized = 0;

   /* Populate architecture information */
   uint8_t arch;
   uint32_t cpu_count;
   char cpu_brand[64];
   get_arch(&arch);
   get_cpu_count(&cpu_count);
   get_cpu_brand(cpu_brand);
   /* Ensure brand is NUL-terminated */
   cpu_brand[63] = '\0';
   g_SysInfo->arch.arch = arch;
   g_SysInfo->arch.cpu_count = cpu_count;
   /* Detect CPU frequency, cache line size, and feature flags via CPUID */
   uint32_t freq = get_cpu_frequency();
   g_SysInfo->arch.cpu_frequency = freq;

   uint32_t cl = get_cache_line_size();
   if (cl == 0) cl = 32; /* fallback */
   g_SysInfo->arch.cache_line_size = cl;

   uint32_t feats = get_cpu_features();
   g_SysInfo->arch.features = feats;
   memcpy(g_SysInfo->arch.cpu_brand, cpu_brand, 64);
   g_SysInfo->arch.cpu_brand[63] = '\0';
}

/**
 * Finalize system initialization
 * Call this after all subsystems have been initialized
 */
void SYS_Finalize()
{
   g_SysInfo->initialized = 1;

   char *arch_str;
   if (g_SysInfo->arch.arch == 1)
      arch_str = "x86";
   else if (g_SysInfo->arch.arch == 2)
      arch_str = "x64";
   else
      arch_str = "aarch64";

   printf("[SYS] Finalized, system info: \n");
   printf("--> Kernel Version: %u.%u\n", g_SysInfo->kernel_major,
          g_SysInfo->kernel_minor);
   printf("--> Architecture: %d (%s)\n", g_SysInfo->arch.arch, arch_str);
   printf("--> CPU Cores: %u\n", g_SysInfo->arch.cpu_count);
   printf("--> CPU Frequency: %u Hz (%u MHz)\n", g_SysInfo->arch.cpu_frequency,
          g_SysInfo->arch.cpu_frequency / 1000 / 1000);
   printf("--> CPU Brand: %s\n", g_SysInfo->arch.cpu_brand);
   printf("--> Total Memory: %u (%u MiB)\n", g_SysInfo->memory.total_memory,
          g_SysInfo->memory.total_memory / 1024 / 1024);
   printf("--> Detected Disks: %u\n", g_SysInfo->disk_count);
}
