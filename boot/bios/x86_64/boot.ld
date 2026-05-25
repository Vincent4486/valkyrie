/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(_start)

SECTIONS
{
    /* Stage1 executes at 0x7C00 in real mode. */
    . = 0x7C00;

    /* Keep boot code in file offset 0 while relocating symbols to 0x7C00. */
    .mbr : AT(0)
    {
        KEEP(*(.mbr))
    }

    /DISCARD/ :
    {
        *(*)
    }
}
