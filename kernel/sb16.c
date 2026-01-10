#include "sb16.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);
void terminal_putchar(char c);
void* malloc(uint32_t size);
void free(void* ptr);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A
#define COLOR_YELLOW 0x0E

// Sound Blaster 16 I/O ports (default base: 0x220)
#define SB16_BASE_DEFAULT 0x220
#define SB16_RESET        0x226
#define SB16_READ_DATA    0x22A
#define SB16_WRITE_DATA   0x22C
#define SB16_WRITE_STATUS 0x22C
#define SB16_READ_STATUS  0x22E
#define SB16_MIXER_ADDR   0x224
#define SB16_MIXER_DATA   0x225
#define SB16_DSP_READ     0x22A
#define SB16_DSP_WRITE    0x22C
#define SB16_DSP_STATUS   0x22E
#define SB16_DSP_RESET    0x226

// DSP commands
#define DSP_CMD_SET_TIME_CONSTANT 0x40
#define DSP_CMD_SET_SAMPLE_RATE   0x41
#define DSP_CMD_SET_BLOCK_SIZE    0x48
#define DSP_CMD_PAUSE_DMA         0xD0
#define DSP_CMD_CONTINUE_DMA      0xD4
#define DSP_CMD_STOP_DMA          0xD9
#define DSP_CMD_SPEAKER_ON        0xD1
#define DSP_CMD_SPEAKER_OFF       0xD3
#define DSP_CMD_VERSION           0xE1

// DSP status bits
#define DSP_STATUS_INPUT_READY  0x80
#define DSP_STATUS_OUTPUT_READY 0x80

// Global state
static uint16_t sb16_base = 0;
static int sb16_initialized = 0;
static int sb16_playing = 0;
static uint8_t* sb16_buffer = NULL;
static uint32_t sb16_buffer_size = 0;

// I/O port helpers
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Wait for DSP to be ready for write
static void sb16_dsp_wait_write(void) {
    int timeout = 1000;
    while ((inb(sb16_base + SB16_WRITE_STATUS) & DSP_STATUS_INPUT_READY) && timeout > 0) {
        for (volatile int i = 0; i < 100; i++);
        timeout--;
    }
}

// Wait for DSP to have data ready for read
static void sb16_dsp_wait_read(void) {
    int timeout = 1000;
    while (!(inb(sb16_base + SB16_READ_STATUS) & DSP_STATUS_OUTPUT_READY) && timeout > 0) {
        for (volatile int i = 0; i < 100; i++);
        timeout--;
    }
}

// Write to DSP
static void sb16_dsp_write(uint8_t value) {
    sb16_dsp_wait_write();
    outb(sb16_base + SB16_DSP_WRITE, value);
}

// Read from DSP
static uint8_t sb16_dsp_read(void) {
    sb16_dsp_wait_read();
    return inb(sb16_base + SB16_DSP_READ);
}

// Reset DSP
static int sb16_dsp_reset(void) {
    // Write 1 to reset port
    outb(sb16_base + SB16_DSP_RESET, 1);
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
    // Write 0 to reset port
    outb(sb16_base + SB16_DSP_RESET, 0);
    
    // Wait for ready byte (0xAA)
    int timeout = 1000;
    while (timeout > 0) {
        if (inb(sb16_base + SB16_READ_STATUS) & DSP_STATUS_OUTPUT_READY) {
            uint8_t ready = inb(sb16_base + SB16_DSP_READ);
            if (ready == 0xAA) {
                return 0;
            }
        }
        for (volatile int i = 0; i < 100; i++);
        timeout--;
    }
    
    return -1;
}

// Detect SB16 at given base address
static int sb16_detect(uint16_t base) {
    char hex[] = "0123456789ABCDEF";
    
    // First, check if ports are accessible
    // Try reading from multiple ports to see if device exists
    uint8_t status_port = inb(base + SB16_READ_STATUS);
    uint8_t data_port = inb(base + SB16_DSP_READ);
    uint8_t reset_port_check = inb(base + SB16_DSP_RESET);
    
    terminal_writestring("  Port check: status=0x");
    terminal_putchar(hex[(status_port >> 4) & 0xF]);
    terminal_putchar(hex[status_port & 0xF]);
    terminal_writestring(", data=0x");
    terminal_putchar(hex[(data_port >> 4) & 0xF]);
    terminal_putchar(hex[data_port & 0xF]);
    terminal_writestring(", reset=0x");
    terminal_putchar(hex[(reset_port_check >> 4) & 0xF]);
    terminal_putchar(hex[reset_port_check & 0xF]);
    terminal_writestring("\n");
    
    // If all ports return 0xFF, device likely doesn't exist
    if (status_port == 0xFF && data_port == 0xFF && reset_port_check == 0xFF) {
        terminal_writestring("  All ports return 0xFF (device not present)\n");
        return -1;
    }
    
    // Try to reset DSP - QEMU's SB16 needs a specific sequence
    terminal_writestring("  Writing reset (1)...\n");
    outb(base + SB16_DSP_RESET, 1);
    
    // Wait for reset (QEMU may need longer)
    for (volatile int i = 0; i < 100000; i++);
    
    // Check status after setting reset
    uint8_t status_after_reset = inb(base + SB16_READ_STATUS);
    terminal_writestring("  Status after reset set: 0x");
    terminal_putchar(hex[(status_after_reset >> 4) & 0xF]);
    terminal_putchar(hex[status_after_reset & 0xF]);
    terminal_writestring("\n");
    
    terminal_writestring("  Writing reset (0)...\n");
    outb(base + SB16_DSP_RESET, 0);
    
    // Wait for device to be ready
    for (volatile int i = 0; i < 50000; i++);
    
    // Check status after releasing reset
    uint8_t status_after_release = inb(base + SB16_READ_STATUS);
    terminal_writestring("  Status after reset release: 0x");
    terminal_putchar(hex[(status_after_release >> 4) & 0xF]);
    terminal_putchar(hex[status_after_release & 0xF]);
    terminal_writestring("\n");
    
    // Poll for ready byte - QEMU's SB16 should respond with 0xAA
    // Try many times with delays
    terminal_writestring("  Polling for 0xAA...\n");
    for (int attempt = 0; attempt < 50; attempt++) {
        // Check if data is ready - try both status check and direct read
        // QEMU's SB16 might not set status bits correctly, so try direct read too
        uint8_t status = inb(base + SB16_READ_STATUS);
        
        // Try reading from data port (QEMU might respond even without status bit set)
        uint8_t ready = inb(base + SB16_DSP_READ);
        
        // Debug output every 10 attempts
        if (attempt % 10 == 0) {
            terminal_writestring("  Attempt ");
            // Print attempt number
            int temp = attempt;
            char num[4];
            int pos = 0;
            if (temp == 0) {
                num[pos++] = '0';
            } else {
                char rev[4];
                int rev_pos = 0;
                while (temp > 0) {
                    rev[rev_pos++] = '0' + (temp % 10);
                    temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    num[pos++] = rev[k];
                }
            }
            num[pos] = '\0';
            terminal_writestring(num);
            terminal_writestring(": status=0x");
            terminal_putchar(hex[(status >> 4) & 0xF]);
            terminal_putchar(hex[status & 0xF]);
            terminal_writestring(", read=0x");
            terminal_putchar(hex[(ready >> 4) & 0xF]);
            terminal_putchar(hex[ready & 0xF]);
            terminal_writestring("\n");
        }
        
        if (ready == 0xAA) {
            terminal_writestring_color("  Found 0xAA!\n", COLOR_GREEN);
            return 0;  // Found!
        }
        
        // Also check if status indicates ready
        if (status & 0x80) {
            ready = inb(base + SB16_DSP_READ);
            if (ready == 0xAA) {
                terminal_writestring_color("  Found 0xAA via status!\n", COLOR_GREEN);
                return 0;  // Found!
            }
        }
        
        // Delay between attempts
        for (volatile int i = 0; i < 20000; i++);
    }
    
    terminal_writestring("  No 0xAA response\n");
    return -1;
}

// Initialize Sound Blaster 16
int sb16_init(void) {
    if (sb16_initialized) {
        return 0;
    }
    
    terminal_writestring("Searching for Sound Blaster 16...\n");
    
    // Try multiple base addresses (common SB16 addresses)
    uint16_t bases[] = {0x220, 0x240, 0x260, 0x280};
    int found = 0;
    
    for (int i = 0; i < 4; i++) {
        terminal_writestring("Trying base 0x");
        // Print hex address
        char hex[] = "0123456789ABCDEF";
        terminal_putchar(hex[(bases[i] >> 12) & 0xF]);
        terminal_putchar(hex[(bases[i] >> 8) & 0xF]);
        terminal_putchar(hex[(bases[i] >> 4) & 0xF]);
        terminal_putchar(hex[bases[i] & 0xF]);
        terminal_writestring("...\n");
        
        if (sb16_detect(bases[i]) == 0) {
            sb16_base = bases[i];
            terminal_writestring_color("SB16 found at 0x", COLOR_GREEN);
            terminal_putchar(hex[(bases[i] >> 12) & 0xF]);
            terminal_putchar(hex[(bases[i] >> 8) & 0xF]);
            terminal_putchar(hex[(bases[i] >> 4) & 0xF]);
            terminal_putchar(hex[bases[i] & 0xF]);
            terminal_writestring("\n");
            found = 1;
            break;
        }
    }
    
    if (!found) {
        terminal_writestring_color("SB16: Device not found at any address\n", COLOR_RED);
        terminal_writestring_color("Note: QEMU needs -soundhw sb16 flag for audio support\n", COLOR_YELLOW);
        terminal_writestring_color("Note: QEMU 4.0+ has known SB16 emulation issues\n", COLOR_YELLOW);
        return -1;
    }
    
    // Reset DSP
    if (sb16_dsp_reset() != 0) {
        terminal_writestring_color("SB16: Reset failed\n", COLOR_RED);
        return -1;
    }
    
    // Enable speaker
    sb16_dsp_write(DSP_CMD_SPEAKER_ON);
    
    sb16_initialized = 1;
    terminal_writestring_color("SB16 initialized successfully\n", COLOR_GREEN);
    
    return 0;
}

// Play PCM audio (8-bit unsigned samples)
int sb16_play_pcm(uint8_t* samples, uint32_t sample_count, uint32_t sample_rate) {
    if (!sb16_initialized || samples == NULL || sample_count == 0) {
        return -1;
    }
    
    // Stop any current playback
    sb16_stop();
    
    // Set sample rate (time constant method - simpler)
    // Time constant = 256 - (1000000 / sample_rate)
    uint32_t time_constant = 256 - (1000000 / sample_rate);
    if (time_constant > 255) time_constant = 255;
    if (time_constant < 0) time_constant = 0;
    
    sb16_dsp_write(DSP_CMD_SET_TIME_CONSTANT);
    sb16_dsp_write((uint8_t)time_constant);
    
    // Play samples in blocks (max 64KB per block, but use smaller blocks for reliability)
    uint32_t offset = 0;
    const uint32_t max_block_size = 16384;  // 16KB blocks
    
    while (offset < sample_count) {
        uint32_t block_size = sample_count - offset;
        if (block_size > max_block_size) {
            block_size = max_block_size;
        }
        
        // Send block size command (size - 1, as per SB16 spec)
        uint32_t block_size_minus_one = block_size - 1;
        sb16_dsp_write(DSP_CMD_SET_BLOCK_SIZE);
        sb16_dsp_write(block_size_minus_one & 0xFF);
        sb16_dsp_write((block_size_minus_one >> 8) & 0xFF);
        
        // Send 8-bit mono auto-init DMA command (0x1C)
        sb16_dsp_write(0x1C);
        
        // Write samples to DSP (one at a time)
        for (uint32_t i = 0; i < block_size; i++) {
            sb16_dsp_write(samples[offset + i]);
        }
        
        offset += block_size;
        
        // Wait for block to finish playing
        // Approximate: samples / sample_rate seconds
        // For 16KB at 22050 Hz: ~0.74 seconds
        // Use a simple delay (this is a simplified implementation)
        uint32_t delay_loops = (block_size * 1000) / sample_rate;
        for (volatile uint32_t i = 0; i < delay_loops * 10000; i++);
    }
    
    sb16_playing = 1;
    return 0;
}

// Stop playback
int sb16_stop(void) {
    if (!sb16_initialized) {
        return -1;
    }
    
    sb16_dsp_write(DSP_CMD_STOP_DMA);
    sb16_playing = 0;
    
    return 0;
}

// Check if playing
int sb16_is_playing(void) {
    return sb16_playing;
}

