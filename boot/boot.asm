; AFOS Bootloader - Multiboot header
; This file sets up the Multiboot header that GRUB expects

section .multiboot_header
header_start:
    ; Multiboot magic number
    dd 0x1BADB002              ; magic
    dd 0x0                     ; flags
    dd -(0x1BADB002 + 0x0)     ; checksum

header_end:

