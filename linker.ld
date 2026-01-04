/* Linker script for AFOS kernel */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB (standard location) */
    . = 1M;

    /* Boot section with multiboot header */
    .boot : ALIGN(4K)
    {
        *(.multiboot_header)
    }

    /* Kernel code section */
    .text : ALIGN(4K)
    {
        *(.text)
    }

    /* Read-only data */
    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    /* Read-write data (initialized) */
    .data : ALIGN(4K)
    {
        *(.data)
    }

    /* Read-write data (uninitialized) */
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
}

