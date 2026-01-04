; AFOS Bootloader - Multiboot header
; This file sets up the Multiboot header that GRUB expects

section .multiboot_header
header_start:
    ; Multiboot magic number
    dd 0x1BADB002              ; magic
    ; Flags: request memory info (no video mode - we'll set it ourselves)
    dd 0x00000003              ; flags: MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO
    dd -(0x1BADB002 + 0x00000003)     ; checksum

header_end:

