#include "pit.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);

// PIT I/O ports
#define PIT_CHANNEL0_DATA 0x40
#define PIT_CHANNEL1_DATA 0x41
#define PIT_CHANNEL2_DATA 0x42
#define PIT_COMMAND       0x43

// PIT command register bits
#define PIT_CHANNEL0      0x00
#define PIT_CHANNEL1      0x40
#define PIT_CHANNEL2      0x80
#define PIT_ACCESS_LATCH 0x00
#define PIT_ACCESS_LO     0x10
#define PIT_ACCESS_HI     0x20
#define PIT_ACCESS_LOHI   0x30
#define PIT_MODE_0        0x00  // Interrupt on terminal count
#define PIT_MODE_2        0x04  // Rate generator
#define PIT_MODE_3        0x06  // Square wave generator
#define PIT_BINARY        0x00
#define PIT_BCD           0x01

// PIT base frequency (1193182 Hz)
#define PIT_BASE_FREQ 1193182

// Global tick counter
static volatile uint32_t pit_ticks = 0;

// I/O port helpers
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Timer interrupt handler (called from ISR)
void pit_timer_handler(void) {
    pit_ticks++;
}

// Initialize PIT to generate interrupts at specified frequency
int pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0 || frequency_hz > PIT_BASE_FREQ) {
        return -1;
    }
    
    // Calculate divisor
    uint32_t divisor = PIT_BASE_FREQ / frequency_hz;
    
    // Send command: Channel 0, Access mode LO/HI, Mode 3 (square wave), Binary
    outb(PIT_COMMAND, PIT_CHANNEL0 | PIT_ACCESS_LOHI | PIT_MODE_3 | PIT_BINARY);
    
    // Send divisor (low byte, then high byte)
    uint8_t low = divisor & 0xFF;
    uint8_t high = (divisor >> 8) & 0xFF;
    outb(PIT_CHANNEL0_DATA, low);
    outb(PIT_CHANNEL0_DATA, high);
    
    pit_ticks = 0;
    
    return 0;
}

// Get current tick count
uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

// Sleep for specified milliseconds
void pit_sleep_ms(uint32_t ms) {
    uint32_t start_ticks = pit_ticks;
    uint32_t target_ticks = start_ticks + ms;  // Assuming 1000 Hz = 1 tick per ms
    
    // Handle potential wraparound (if target_ticks wraps, we need to check differently)
    // Simple busy-wait
    uint32_t iterations = 0;
    while (pit_ticks < target_ticks) {
        __asm__ volatile("pause");
        iterations++;
        // Safety check: if we've been waiting too long (more than 2x the expected time), break
        // This handles the case where pit_ticks isn't incrementing
        if (iterations > (ms * 2000000)) {  // Rough estimate: pause takes ~0.5ns, so 2M iterations per ms
            // Timer might not be working, break anyway
            break;
        }
    }
}

