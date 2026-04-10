/* SPDX-License-Identifier: GPL-3.0-only */

ENTRY(_start)
/* Two PT_LOAD segments: executable code+rodata (txt) and writable data (dat).
   The multiboot header lives at the very start of the txt segment so QEMU/GRUB
   can find the magic value within the first 8 KiB of the loaded image. */
PHDRS
{
   txt   PT_LOAD FLAGS(READ | EXEC);
   dat   PT_LOAD FLAGS(READ | WRITE);
}
/* Produce ELF output (default) instead of raw binary so the bootloader can
   load an ELF image. 
   
   Kernel metadata (SYS_Info, BOOT_Info copies, module registry) now lives in
   kernel-owned BSS/heap, so this script only defines the kernel image base.
   
   With dynamic linking, allow undefined symbols (will be resolved at runtime
   via PLT/GOT patching after libraries are loaded).
*/

/* Default kernel load address (override with --defsym=KERNEL_LOAD_ADDR=...) */
KERNEL_LOAD_ADDR = DEFINED(KERNEL_LOAD_ADDR) ? KERNEL_LOAD_ADDR : 0x00100000;
phys = KERNEL_LOAD_ADDR;

/* Allow undefined symbols - they will be resolved at runtime by dylib loader */
/* This lets kernel code call library functions without explicit EXTERN declarations */

SECTIONS
{
   /* All kernel sections start at the configured physical base. */
   . = phys;
   __kernel_image_start = .;

   /* Multiboot header MUST come first inside the first PT_LOAD segment so
      QEMU/GRUB can find the magic value within the first 8 KiB of the image.
      It is tiny (12 bytes) and harmlessly precedes the real entry point. */
   .multiboot          : { __multiboot_start = .; KEEP(*(.multiboot)) } :txt

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
