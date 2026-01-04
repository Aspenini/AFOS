; AFOS Kernel Entry Point
; This is the first code that runs after the bootloader hands control to the kernel

section .text
global _start
extern kernel_main

_start:
    ; Set up stack pointer
    mov esp, stack_top
    
    ; Push multiboot info (if available)
    push ebx    ; multiboot info structure
    push eax    ; multiboot magic number
    
    ; Call kernel main
    call kernel_main
    
    ; If kernel_main returns, halt
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB stack
stack_top:

