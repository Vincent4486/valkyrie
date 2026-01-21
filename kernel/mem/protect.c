// SPDX-License-Identifier: AGPL-3.0-or-later

#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>

uintptr_t __stack_chk_guard = 0xDEADBEEF;

void __stack_chk_fail_local(void)
{
   printf("\n");
   printf("╔════════════════════════════════════╗\n");
   printf("║  STACK SMASHING DETECTED!          ║\n");
   printf("║  Buffer overflow in stack frame    ║\n");
   printf("╚════════════════════════════════════╝\n");
   g_HalIoOperations->Panic(); // or infinite loop
}