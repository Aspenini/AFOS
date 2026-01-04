#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "keyboard.h"
#include "filesystem.h"
#include "shell.h"

// Simple VGA text mode functions
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static uint16_t* const VGA_BUFFER = (uint16_t*) VGA_MEMORY;

static size_t terminal_row = 0;
static size_t terminal_column = 0;
static uint8_t terminal_color = 0x0F; // White on black

static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) __attribute__((unused));
static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void terminal_initialize(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            VGA_BUFFER[index] = vga_entry(' ', terminal_color);
        }
    }
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
}

void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    VGA_BUFFER[index] = vga_entry(c, color);
}

// Scroll terminal up by one line
static void terminal_scroll(void) {
    // Shift all lines up by one
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t src_index = y * VGA_WIDTH + x;
            const size_t dst_index = (y - 1) * VGA_WIDTH + x;
            VGA_BUFFER[dst_index] = VGA_BUFFER[src_index];
        }
    }
    
    // Clear the bottom line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        VGA_BUFFER[index] = vga_entry(' ', terminal_color);
    }
}

// Clear the entire terminal
void terminal_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            VGA_BUFFER[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
    } else if (c == '\b') {
        // Handle backspace - move cursor back and erase character
        if (terminal_column > 0) {
            terminal_column--;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        } else if (terminal_row > 0) {
            // Wrap to previous line
            terminal_row--;
            terminal_column = VGA_WIDTH - 1;
            terminal_putentryat(' ', terminal_color, terminal_column, terminal_row);
        }
    } else {
        terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
        if (++terminal_column == VGA_WIDTH) {
            terminal_column = 0;
            if (++terminal_row >= VGA_HEIGHT) {
                terminal_scroll();
                terminal_row = VGA_HEIGHT - 1;
            }
        }
    }
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        terminal_putchar(data[i]);
    }
}

void terminal_writestring(const char* data) {
    size_t len = 0;
    while (data[len] != '\0') {
        len++;
    }
    terminal_write(data, len);
}

// Write string with specific color, then restore default
void terminal_writestring_color(const char* data, uint8_t color) {
    uint8_t old_color = terminal_color;
    terminal_setcolor(color);
    terminal_writestring(data);
    terminal_setcolor(old_color);
}

// Color constants
#define COLOR_DEFAULT 0x0F  // White on black
#define COLOR_RED     0x0C  // Light red on black
#define COLOR_GREEN   0x0A  // Light green on black
#define COLOR_YELLOW  0x0E  // Yellow on black
#define COLOR_BLUE    0x09  // Light blue on black
#define COLOR_CYAN    0x0B  // Light cyan on black

// Kernel main function
void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info __attribute__((unused))) {
    // Initialize terminal
    terminal_initialize();
    
    // Display welcome message with color
    terminal_writestring_color("AFOS - Aspen Feltner Operating System\n", COLOR_CYAN);
    terminal_writestring("Kernel loaded successfully!\n");
    terminal_writestring("32-bit kernel running...\n");
    
    // Display multiboot info
    if (multiboot_magic == 0x2BADB002) {
        terminal_writestring("Multiboot bootloader detected\n");
    } else {
        terminal_writestring("Warning: Invalid multiboot magic number\n");
    }
    
    // Initialize GDT
    terminal_writestring("Initializing GDT...\n");
    gdt_init();
    
    // Initialize IDT
    terminal_writestring("Initializing IDT...\n");
    idt_init();
    
    // Initialize PIC
    terminal_writestring("Initializing PIC...\n");
    pic_init();
    
    // Initialize keyboard
    terminal_writestring("Initializing keyboard...\n");
    keyboard_init();
    
    // Initialize filesystem
    terminal_writestring("Initializing filesystem...\n");
    fs_init();
    
    // Load files from sys/ directory (generated at build time)
    terminal_writestring("Loading system files...\n");
    sysfs_initialize();
    
    // Enable interrupts
    terminal_writestring("Enabling interrupts...\n");
    __asm__ volatile("sti");
    
    terminal_writestring("\n=== AFOS Shell ===\n");
    terminal_writestring("Type 'ls' or 'dir' to list files, 'cd <dir>' to change directory\n");
    terminal_writestring("Use 'cd ..' to go to parent directory\n\n");
    
    // Initialize and run shell
    shell_init();
    shell_run();
}

