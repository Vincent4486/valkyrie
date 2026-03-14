// SPDX-License-Identifier: BSD-3-Clause

#define SYS_WRITE 4
#define SYS_EXIT 1

__attribute__((noreturn)) void _start(void)
{
   static const char msg[] = "selftest: syscall write ok\n";
   volatile int result = 0;

   __asm__ __volatile__("int $0x80"
                        : "=a"(result)
                        : "a"(SYS_WRITE), "b"(1), "c"(msg),
                          "d"((unsigned int)(sizeof(msg) - 1))
                        : "memory");

   __asm__ __volatile__("int $0x80"
                        : "=a"(result)
                        : "a"(SYS_EXIT), "b"(0)
                        : "memory");

   for (;;)
   {
   }
}
