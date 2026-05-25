/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(_start)

SECTIONS
{
    /* Core stage is loaded by stage1 to linear address 0x8000. */
    . = 0x7E00;

    .entry : AT(0)
    {
        *(.entry*)
    }

    .text :
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
