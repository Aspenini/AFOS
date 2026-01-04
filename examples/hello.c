/*
 * Example AFOS Program
 * 
 * This is a minimal example program that can be compiled into an AFOS binary.
 * 
 * To compile:
 *   1. x86_64-elf-gcc -m32 -nostdlib -ffreestanding -I../include -c hello.c -o hello.o
 *   2. x86_64-elf-ld -m elf_i386 -Ttext 0x1000 --oformat=binary -o hello.bin hello.o
 *   3. ../tools/afos-pack hello.bin hello.afos
 * 
 * Note: This is a simplified example. In a real program, you'd need to link
 * against kernel functions properly.
 */

// Minimal program entry point
// Note: In a real implementation, you'd need proper function declarations
// and linking against kernel functions

int main(int argc, char** argv) {
    // This is a placeholder - actual implementation would call kernel functions
    // like terminal_writestring("Hello from AFOS program!\n");
    return 0;
}

