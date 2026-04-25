/* SPDX-License-Identifier: BSD-3-Clause */

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
