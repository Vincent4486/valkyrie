// SPDX-License-Identifier: AGPL-3.0-or-later

#include <hal/io.h>
#include <mem/mm_kernel.h>
#include <std/stdio.h>

uintptr_t __stack_chk_guard = 0xDEADBEEF;

void __stack_chk_fail_local(void)
{
   logfmt(LOG_ERROR, "\n");
   logfmt(LOG_ERROR, "╔════════════════════════════════════╗\n");
   logfmt(LOG_ERROR, "║  STACK SMASHING DETECTED!          ║\n");
   logfmt(LOG_ERROR, "║  Buffer overflow in stack frame    ║\n");
   logfmt(LOG_ERROR, "╚════════════════════════════════════╝\n");
   g_HalIoOperations->Panic(); // or infinite loop
}

void __stack_chk_fail(void)
{
   logfmt(LOG_ERROR, "\n");
   logfmt(LOG_ERROR, "╔════════════════════════════════════╗\n");
   logfmt(LOG_ERROR, "║  STACK SMASHING DETECTED!          ║\n");
   logfmt(LOG_ERROR, "║  Buffer overflow in stack frame    ║\n");
   logfmt(LOG_ERROR, "╚════════════════════════════════════╝\n");
   g_HalIoOperations->Panic(); // or infinite loop
}