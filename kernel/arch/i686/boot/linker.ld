/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(_start)
/* Program headers: place the multiboot header in a small PT_LOAD (mb),
    put executable code in PT_LOAD (txt) and writable data in PT_LOAD (dat). */
PHDRS
{
   mb    PT_LOAD FLAGS(READ);
   txt   PT_LOAD FLAGS(READ | EXEC);
   dat   PT_LOAD FLAGS(READ | WRITE);
}
/* Produce ELF output (default) instead of raw binary so the bootloader can
   load an ELF image. 
   
   Memory layout (all regions 1 KiB block-aligned):
   - 0x00100000 - 0x00800000: Dynamic libraries (dylib) - 7 MiB
   - 0x00930000             : g_SysInfo  (SYS_Info)
   - 0x00931000             : s_bootInfo (BOOT_Info)  [+1 KiB block]
   - 0x00A00000 onwards     : Kernel code and data
   
   With dynamic linking, allow undefined symbols (will be resolved at runtime
   via PLT/GOT patching after libraries are loaded).
*/

K_MEM_BLOCK_SIZE = 1024;

DYLIB_START = 0x400 * K_MEM_BLOCK_SIZE;      /* 0x00100000 */
DYLIB_SIZE  = 0x1C00 * K_MEM_BLOCK_SIZE;     /* 0x700000 (7 MiB) */

SYS_INFO_START  = 0x24C0 * K_MEM_BLOCK_SIZE; /* 0x00930000 */
SYS_INFO_SIZE   = 4 * K_MEM_BLOCK_SIZE;      /* 0x1000 (1 KiB block) */

BOOT_INFO_START = (0x24C0 + 4) * K_MEM_BLOCK_SIZE; /* 0x00931000 - 4 blocks (0x1000 bytes) after SYS_INFO */

/* Kernel starts at 10 MiB */
phys = 0x00A00000;

/* Allow undefined symbols - they will be resolved at runtime by dylib loader */
/* This lets kernel code call library functions without explicit EXTERN declarations */

SECTIONS
{
    /* Place the multiboot header section at the start of the output file
       so GRUB can find it within the first 8KiB. Do not place it at the
       runtime physical address. */
   .multiboot          : { __multiboot_start = .; KEEP(*(.multiboot)) } :mb

   /* Now set the runtime origin for the kernel sections */
   . = phys;

   .entry              : { __entry_start = .;      *(.entry)   } :txt
    
    .text               : { __text_start = .;       *(.text .text.*)    } :txt

    /* Procedure Linkage Table - for indirect function calls to library functions */
    .plt                : { *(.plt) } :txt

    /* Place read-only data next so it stays in the executable PT_LOAD */
    .rodata             : { __rodata_start = .;     *(.rodata .rodata.*)    } :txt

    /* Writable data goes after read-only segments to avoid overlap */
    .data               : { __data_start = .;       *(.data .data.*)    } :dat
    
   /* Global Offset Table - stores resolved addresses for indirect references */
   .got                : { *(.got) } :dat
   .got.plt            : { *(.got.plt) } :dat
    
    /* Dynamic symbol sections - expose so runtime can resolve kernel relocations */
    _kernel_dynsym_start = .;
    .dynsym             : { *(.dynsym) }
    _kernel_dynsym_end = .;

    _kernel_dynstr_start = .;
    .dynstr             : { *(.dynstr) }
    _kernel_dynstr_end = .;
    
    /* Relocations - kernel loader will apply these after loading libraries */
    .rel.dyn            : { _kernel_rel_dyn_start = .; *(.rel.dyn) _kernel_rel_dyn_end = .; }
    
    .rel.plt            : { _kernel_rel_plt_start = .; *(.rel.plt) _kernel_rel_plt_end = .; }
    
    .bss                : ALIGN(1024) { __bss_start = .;        *(.bss .bss.*)     }
    
    __end = .;
}
