#include "vesa.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C

// VESA mode info
static struct vesa_mode_info vesa_info;
static int vesa_initialized = 0;
static int vesa_graphics_mode = 0;

// Read from I/O port
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Write to I/O port
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Read from I/O port (16-bit)
static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Write to I/O port (16-bit)
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

// Wait for VGA to be ready (unused for now, but may be needed later)
// static void vesa_wait_vsync(void) {
//     // Wait for vertical retrace
//     while (inb(0x3DA) & 0x08);
//     while (!(inb(0x3DA) & 0x08));
// }

// Initialize VGA system
int vesa_init(void) {
    if (vesa_initialized) {
        return 0;
    }
    
    vesa_initialized = 1;
    return 0;
}

// Set VGA graphics mode (mode 13h: 320x200x8)
// Direct VGA register programming - works in BIOS mode without GRUB
int vesa_set_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    if (!vesa_initialized) {
        if (vesa_init() != 0) {
            return -1;
        }
    }
    
    // Only support VGA mode 13h (320x200x8) for now
    // This is the simplest and most compatible graphics mode
    if (width == 320 && height == 200 && bpp == 8) {
        __asm__ volatile("cli");
        
        // Set VGA mode 13h (320x200x256 colors)
        // This uses the mode register at 0x3C0
        outb(0x3C2, 0x63); // Miscellaneous output
        
        // Set sequencer registers
        outb(0x3C4, 0x00); outb(0x3C5, 0x03); // Reset
        outb(0x3C4, 0x01); outb(0x3C5, 0x01); // Clocking mode
        outb(0x3C4, 0x02); outb(0x3C5, 0x0F); // Map Mask (all planes enabled for chain-4)
        outb(0x3C4, 0x04); outb(0x3C5, 0x0E); // Memory mode (chain 4 enabled, odd/even disabled, extended memory)
        
        // Set CRT controller
        outb(0x3D4, 0x11); outb(0x3D5, inb(0x3D5) & 0x7F); // Unlock
        
        // Set mode 13h parameters
        outb(0x3D4, 0x00); outb(0x3D5, 0x5F); // Horizontal total
        outb(0x3D4, 0x01); outb(0x3D5, 0x4F); // Horizontal display end
        outb(0x3D4, 0x04); outb(0x3D5, 0x11); // Start horizontal blanking
        outb(0x3D4, 0x05); outb(0x3D5, 0x00); // End horizontal blanking
        outb(0x3D4, 0x06); outb(0x3D5, 0xBF); // Start horizontal retrace
        outb(0x3D4, 0x07); outb(0x3D5, 0x1F); // End horizontal retrace
        outb(0x3D4, 0x09); outb(0x3D5, 0x40); // Max scan line
        outb(0x3D4, 0x10); outb(0x3D5, 0x9C); // Start vertical retrace
        outb(0x3D4, 0x11); outb(0x3D5, 0x8E); // End vertical retrace
        outb(0x3D4, 0x12); outb(0x3D5, 0x8F); // Vertical display end
        outb(0x3D4, 0x13); outb(0x3D5, 0x28); // Offset
        outb(0x3D4, 0x14); outb(0x3D5, 0x00); // Underline location
        outb(0x3D4, 0x15); outb(0x3D5, 0x96); // Start vertical blank
        outb(0x3D4, 0x16); outb(0x3D5, 0xB9); // End vertical blank
        outb(0x3D4, 0x17); outb(0x3D5, 0xE3); // Mode control
        
        // Additional CRT controller settings for mode 13h
        outb(0x3D4, 0x08); outb(0x3D5, 0x00); // Preset row scan
        outb(0x3D4, 0x0A); outb(0x3D5, 0x00); // Cursor start
        outb(0x3D4, 0x0B); outb(0x3D5, 0x00); // Cursor end
        outb(0x3D4, 0x0C); outb(0x3D5, 0x00); // Start address high
        outb(0x3D4, 0x0D); outb(0x3D5, 0x00); // Start address low
        
        // Set graphics controller (complete setup for mode 13h)
        // Mode 13h uses chain-4 addressing for 256 colors
        outb(0x3CE, 0x00); outb(0x3CF, 0x00); // Set/Reset
        outb(0x3CE, 0x01); outb(0x3CF, 0x00); // Enable Set/Reset
        outb(0x3CE, 0x02); outb(0x3CF, 0x00); // Color Compare
        outb(0x3CE, 0x03); outb(0x3CF, 0x00); // Data Rotate
        outb(0x3CE, 0x04); outb(0x3CF, 0x00); // Read Map Select
        outb(0x3CE, 0x05); outb(0x3CF, 0x40); // Mode (256 color mode, bit 6 = 256 color)
        outb(0x3CE, 0x06); outb(0x3CF, 0x05); // Miscellaneous (graphics mode, chain 4)
        outb(0x3CE, 0x07); outb(0x3CF, 0x0F); // Color Don't Care
        outb(0x3CE, 0x08); outb(0x3CF, 0xFF); // Bit Mask
        
        // Set attribute controller
        inb(0x3DA); // Reset flip-flop
        for (int i = 0; i < 16; i++) {
            outb(0x3C0, i);
            outb(0x3C0, i);
        }
        outb(0x3C0, 0x20); // Enable palette
        
        // Clear the framebuffer immediately after mode switch
        uint8_t* fb = (uint8_t*)0xA0000;
        for (uint32_t i = 0; i < 320 * 200; i++) {
            fb[i] = 0;  // Clear to black
        }
        
        vesa_info.width = 320;
        vesa_info.height = 200;
        vesa_info.bpp = 8;
        vesa_info.pitch = 320;
        vesa_info.framebuffer = 0xA0000; // VGA mode 13h framebuffer
        
        __asm__ volatile("sti");
        vesa_graphics_mode = 1;
        return 0;
    }
    
    // For other modes, we'd need VBE which requires BIOS calls
    // For now, only mode 13h is supported
    terminal_writestring_color("Error: Only 320x200x8 mode is supported\n", COLOR_RED);
    terminal_writestring_color("Please use: gfx_init(320, 200, 8)\n", COLOR_RED);
    return -1;
}

// Switch back to text mode
void vesa_switch_to_text_mode(void) {
    if (!vesa_graphics_mode) {
        return;
    }
    
    // Disable interrupts
    __asm__ volatile("cli");
    
    // Switch to text mode (mode 3 = 80x25 text)
    // This is done by setting VGA mode register
    outb(0x3C2, 0xE3); // Miscellaneous output
    outb(0x3D4, 0x00); // Start horizontal total
    outb(0x3D5, 0x5F);
    outb(0x3D4, 0x01);
    outb(0x3D5, 0x4F);
    outb(0x3D4, 0x02);
    outb(0x3D5, 0x50);
    outb(0x3D4, 0x03);
    outb(0x3D5, 0x82);
    outb(0x3D4, 0x04);
    outb(0x3D5, 0x55);
    outb(0x3D4, 0x05);
    outb(0x3D5, 0x81);
    outb(0x3D4, 0x06);
    outb(0x3D5, 0xBF);
    outb(0x3D4, 0x07);
    outb(0x3D5, 0x1F);
    outb(0x3D4, 0x08);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x09);
    outb(0x3D5, 0x4F);
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    outb(0x3D4, 0x0B);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0C);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0D);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0E);
    outb(0x3D5, 0x00);
    outb(0x3D4, 0x0F);
    outb(0x3D5, 0x00);
    
    // Clear screen
    uint16_t* vga = (uint16_t*)0xB8000;
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20; // White on black, space
    }
    
    vesa_graphics_mode = 0;
    
    // Re-enable interrupts
    __asm__ volatile("sti");
}

// Get framebuffer address
uint32_t vesa_get_framebuffer_addr(void) {
    return vesa_info.framebuffer;
}

uint16_t vesa_get_width(void) {
    return vesa_info.width;
}

uint16_t vesa_get_height(void) {
    return vesa_info.height;
}

uint8_t vesa_get_bpp(void) {
    return vesa_info.bpp;
}

uint16_t vesa_get_pitch(void) {
    return vesa_info.pitch;
}

