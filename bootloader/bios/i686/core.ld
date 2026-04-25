/* SPDX-License-Identifier: BSD-3-Clause */

ENTRY(_start)

SECTIONS
{
    /* Core stage is loaded by stage1 to linear address 0x8000. */
    . = 0x8000;

    .text : AT(0)
    {
        *(.text*)
        *(.rodata*)
        *(.data*)
    }

    .bss (NOLOAD) :
    {
        *(.bss*)
        *(COMMON)
    }
}
