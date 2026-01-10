#include "ac97.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);
void terminal_putchar(char c);
void* malloc(uint32_t size);
void free(void* ptr);
extern void pit_sleep_ms(uint32_t ms);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A
#define COLOR_YELLOW 0x0E

// AC97 PCI vendor/device IDs
#define AC97_VENDOR_ID_INTEL 0x8086
#define AC97_VENDOR_ID_ENSONIQ 0x1274
#define AC97_DEVICE_ID_ICH    0x2415  // ICH AC97 (common)
#define AC97_DEVICE_ID_ICH4   0x24C5  // ICH4 AC97
#define AC97_DEVICE_ID_ICH5   0x266E  // ICH5 AC97
#define AC97_DEVICE_ID_ES1370 0x5000  // Ensoniq ES1370 (QEMU default)

// AC97 registers (memory-mapped I/O)
#define AC97_RESET            0x00
#define AC97_MASTER_VOLUME    0x02
#define AC97_PCM_OUT_VOLUME   0x18
#define AC97_EXTENDED_STATUS  0x28
#define AC97_PCM_FRONT_DAC_RATE 0x2C
#define AC97_PCM_SURR_DAC_RATE 0x2E
#define AC97_PCM_LFE_DAC_RATE  0x30
#define AC97_PCM_LR_ADC_RATE   0x32
#define AC97_PCM_MIC_ADC_RATE  0x34

// AC97 Native Audio Mixer Registers (NAM) - I/O ports
#define AC97_NAM_BASE         0x00  // Base offset for NAM registers
#define AC97_NAM_RESET        0x00
#define AC97_NAM_MASTER_VOL   0x02
#define AC97_NAM_PCM_OUT_VOL  0x18

// AC97 Bus Master Registers (BMR) - memory-mapped or I/O
#define AC97_BMR_PO_BDBAR     0x10  // PCM Out Buffer Descriptor Base Address (32-bit)
#define AC97_BMR_PO_CIV       0x14  // PCM Out Current Index Value (8-bit)
#define AC97_BMR_PO_LVI       0x15  // PCM Out Last Valid Index (8-bit)
#define AC97_BMR_PO_SR        0x16  // PCM Out Status Register (16-bit)
#define AC97_BMR_PO_PICB      0x18  // PCM Out Position in Current Buffer (16-bit)
#define AC97_BMR_PO_CR        0x1B  // PCM Out Control Register (8-bit)

// Buffer Descriptor structure (16 bytes)
typedef struct {
    uint32_t address;      // Physical address of buffer (must be 32-byte aligned)
    uint16_t length;       // Buffer length in samples (bit 15 = IOC interrupt enable)
    uint16_t reserved;
} __attribute__((packed)) ac97_bd_t;

// AC97 Status Register bits
#define AC97_SR_DCH           0x01  // DMA Controller Halt
#define AC97_SR_CELV          0x02  // Current equals Last Valid
#define AC97_SR_LVBCI         0x04  // Last Valid Buffer Completion Interrupt
#define AC97_SR_BCIS          0x08  // Buffer Completion Interrupt Status

// AC97 Control Register bits
#define AC97_CR_RPBM          0x01  // Reset PCM Out Bus Master
#define AC97_CR_RR            0x02  // Reset Registers
#define AC97_CR_LVBIE         0x04  // Last Valid Buffer Interrupt Enable
#define AC97_CR_FEIE           0x08  // FIFO Error Interrupt Enable
#define AC97_CR_IOCE           0x10  // Interrupt on Completion Enable

// Global AC97 state
static struct {
    uint32_t mmio_base;      // Memory-mapped I/O base (Bus Master registers)
    uint32_t io_base;        // I/O port base (Native Audio Mixer)
    uint8_t* buffer;         // Audio buffer
    uint32_t buffer_size;    // Buffer size
    ac97_bd_t* bdl;          // Buffer Descriptor List
    uint32_t bdl_phys;       // Physical address of BDL
    int initialized;
    int playing;
} g_ac97 = {0};

// I/O port helpers
static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t result;
    __asm__ volatile("inl %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

// Memory-mapped I/O helpers
static inline uint16_t mmio_read16(uint32_t addr) {
    return *(volatile uint16_t*)addr;
}

static inline void mmio_write16(uint32_t addr, uint16_t value) {
    *(volatile uint16_t*)addr = value;
}

static inline uint32_t mmio_read32(uint32_t addr) {
    return *(volatile uint32_t*)addr;
}

static inline void mmio_write32(uint32_t addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

static inline uint8_t mmio_read8(uint32_t addr) {
    return *(volatile uint8_t*)addr;
}

static inline void mmio_write8(uint32_t addr, uint8_t value) {
    *(volatile uint8_t*)addr = value;
}

// PCI configuration space access
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Find AC97 PCI device
static int pci_find_ac97(uint8_t* bus, uint8_t* slot, uint8_t* func) {
    // Scan PCI buses
    for (uint8_t b = 0; b < 255; b++) {
        for (uint8_t s = 0; s < 32; s++) {
            uint32_t vendor_device = pci_read_config(b, s, 0, 0);
            uint16_t vendor = vendor_device & 0xFFFF;
            uint16_t device = (vendor_device >> 16) & 0xFFFF;
            
            // Skip invalid devices (vendor 0xFFFF means no device)
            if (vendor == 0xFFFF) {
                continue;
            }
            
            // Check class code (0x04 = Multimedia, 0x01 = Audio, 0x00 = AC97)
            uint32_t class_rev = pci_read_config(b, s, 0, 0x08);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass = (class_rev >> 16) & 0xFF;
            uint8_t prog_if = (class_rev >> 8) & 0xFF;
            
            // AC97 audio device: Class 0x04, Subclass 0x01, Prog IF 0x00
            if (class_code == 0x04 && subclass == 0x01 && prog_if == 0x00) {
                *bus = b;
                *slot = s;
                *func = 0;
                return 0;
            }
            
            // Also check for known AC97 variants by vendor/device ID
            if (vendor == AC97_VENDOR_ID_INTEL) {
                if (device == AC97_DEVICE_ID_ICH || 
                    device == AC97_DEVICE_ID_ICH4 || 
                    device == AC97_DEVICE_ID_ICH5) {
                    *bus = b;
                    *slot = s;
                    *func = 0;
                    return 0;
                }
            }
            // Check for Ensoniq ES1370 (QEMU's default AC97)
            if (vendor == AC97_VENDOR_ID_ENSONIQ && device == AC97_DEVICE_ID_ES1370) {
                *bus = b;
                *slot = s;
                *func = 0;
                return 0;
            }
        }
    }
    return -1;
}

// Initialize AC97
int ac97_init(void) {
    if (g_ac97.initialized) {
        return 0;
    }
    
    terminal_writestring("Searching for AC97 audio device...\n");
    
    // Find PCI device
    uint8_t bus, slot, func;
    if (pci_find_ac97(&bus, &slot, &func) != 0) {
        terminal_writestring_color("AC97: Device not found\n", COLOR_RED);
        return -1;
    }
    
    terminal_writestring("AC97 found on PCI bus ");
    char bus_str[4];
    int temp = bus;
    int pos = 0;
    if (temp == 0) {
        bus_str[pos++] = '0';
    } else {
        char rev[4];
        int rev_pos = 0;
        while (temp > 0) {
            rev[rev_pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        for (int j = rev_pos - 1; j >= 0; j--) {
            bus_str[pos++] = rev[j];
        }
    }
    bus_str[pos] = '\0';
    terminal_writestring(bus_str);
    terminal_writestring("\n");
    
    // Read BAR0 (Native Audio Mixer base - I/O ports)
    uint32_t bar0 = pci_read_config(bus, slot, func, 0x10);
    if ((bar0 & 0x01) == 0) {
        terminal_writestring_color("AC97: BAR0 not I/O space, trying as memory\n", COLOR_YELLOW);
        // Some implementations use memory-mapped for NAM
        g_ac97.io_base = 0;  // Will use MMIO instead
        g_ac97.mmio_base = bar0 & 0xFFFFFFF0;
    } else {
        g_ac97.io_base = bar0 & 0xFFFC;
    }
    
    // Read BAR1 (Bus Master base - should be memory-mapped)
    uint32_t bar1 = pci_read_config(bus, slot, func, 0x14);
    if ((bar1 & 0x01) == 0) {
        // Memory-mapped
        g_ac97.mmio_base = bar1 & 0xFFFFFFF0;
        terminal_writestring("AC97: BAR1 is memory-mapped at 0x");
        // Print hex address
        char hex[] = "0123456789ABCDEF";
        uint32_t addr = g_ac97.mmio_base;
        for (int i = 28; i >= 0; i -= 4) {
            terminal_putchar(hex[(addr >> i) & 0xF]);
        }
        terminal_writestring("\n");
    } else {
        // I/O space - try to use it anyway (some implementations allow this)
        uint16_t bm_io_base = bar1 & 0xFFFC;
        terminal_writestring("AC97: BAR1 is I/O space at 0x");
        char hex[] = "0123456789ABCDEF";
        uint16_t addr = bm_io_base;
        for (int i = 12; i >= 0; i -= 4) {
            terminal_putchar(hex[(addr >> i) & 0xF]);
        }
        terminal_writestring(" (will try I/O access)\n");
        // Store as MMIO base but we'll use I/O ports
        g_ac97.mmio_base = bm_io_base;
    }
    
    // Enable bus mastering and I/O space
    uint32_t command = pci_read_config(bus, slot, func, 0x04);
    command |= 0x05;  // Enable I/O space and bus mastering
    pci_write_config(bus, slot, func, 0x04, command);
    
    // Reset AC97 (only if we have I/O base)
    if (g_ac97.io_base != 0) {
        outw(g_ac97.io_base + AC97_NAM_RESET, 0x0000);
        for (volatile int i = 0; i < 10000; i++);
        outw(g_ac97.io_base + AC97_NAM_RESET, 0x0001);
        for (volatile int i = 0; i < 10000; i++);
        
        // Set master volume
        outw(g_ac97.io_base + AC97_NAM_MASTER_VOL, 0x0000);  // 0dB (max volume)
        
        // Set PCM output volume
        outw(g_ac97.io_base + AC97_NAM_PCM_OUT_VOL, 0x0000);  // 0dB
    } else if (g_ac97.mmio_base != 0) {
        // Memory-mapped reset
        mmio_write16(g_ac97.mmio_base + AC97_RESET, 0x0000);
        for (volatile int i = 0; i < 10000; i++);
        mmio_write16(g_ac97.mmio_base + AC97_RESET, 0x0001);
        for (volatile int i = 0; i < 10000; i++);
        
        // Set volumes via MMIO
        mmio_write16(g_ac97.mmio_base + AC97_MASTER_VOLUME, 0x0000);
        mmio_write16(g_ac97.mmio_base + AC97_PCM_OUT_VOLUME, 0x0000);
    }
    
    g_ac97.initialized = 1;
    terminal_writestring_color("AC97 initialized successfully\n", COLOR_GREEN);
    
    return 0;
}

// Get physical address (simplified - assumes identity mapping)
static uint32_t virt_to_phys(void* virt) {
    // In a real OS, we'd use page tables. For now, assume identity mapping.
    return (uint32_t)virt;
}

// Play PCM audio using DMA
int ac97_play_pcm(uint8_t* samples, uint32_t sample_count, uint32_t sample_rate) {
    if (!g_ac97.initialized || samples == NULL || sample_count == 0 || g_ac97.mmio_base == 0) {
        terminal_writestring_color("AC97: Cannot play - not initialized or no MMIO\n", COLOR_RED);
        return -1;
    }
    
    // Stop any current playback
    ac97_stop();
    
    // Set sample rate via NAM (Native Audio Mixer)
    if (g_ac97.io_base != 0) {
        outw(g_ac97.io_base + AC97_PCM_FRONT_DAC_RATE, sample_rate);
    }
    
    // Allocate buffer descriptor list (need at least 1, use 2 for simplicity)
    // BDL must be 16-byte aligned
    uint32_t bdl_size = sizeof(ac97_bd_t) * 2;
    g_ac97.bdl = (ac97_bd_t*)malloc(bdl_size);
    if (g_ac97.bdl == NULL) {
        terminal_writestring_color("AC97: Failed to allocate BDL\n", COLOR_RED);
        return -1;
    }
    
    // Clear BDL
    for (uint32_t i = 0; i < bdl_size; i++) {
        ((uint8_t*)g_ac97.bdl)[i] = 0;
    }
    
    // Allocate audio buffer (must be 32-byte aligned, but malloc should handle this)
    // For simplicity, use the provided samples buffer directly
    // In a real implementation, we'd copy to an aligned buffer
    g_ac97.buffer = samples;
    g_ac97.buffer_size = sample_count;
    
    // Set up buffer descriptors
    // BD 0: Main audio buffer
    g_ac97.bdl[0].address = virt_to_phys(samples);
    g_ac97.bdl[0].length = sample_count | 0x8000;  // Set IOC bit for interrupt
    g_ac97.bdl[0].reserved = 0;
    
    // BD 1: Empty (marks end)
    g_ac97.bdl[1].address = 0;
    g_ac97.bdl[1].length = 0;
    g_ac97.bdl[1].reserved = 0;
    
    // Get physical address of BDL
    g_ac97.bdl_phys = virt_to_phys(g_ac97.bdl);
    
    // Reset PCM Out Bus Master
    if (g_ac97.mmio_base < 0x10000) {
        // I/O space access
        outb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_CR, AC97_CR_RPBM);
        for (volatile int i = 0; i < 1000; i++);
        outb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_CR, 0);
    } else {
        // Memory-mapped access (CR is 8-bit)
        uint8_t cr = mmio_read8(g_ac97.mmio_base + AC97_BMR_PO_CR);
        mmio_write8(g_ac97.mmio_base + AC97_BMR_PO_CR, cr | AC97_CR_RPBM);
        for (volatile int i = 0; i < 1000; i++);
        mmio_write8(g_ac97.mmio_base + AC97_BMR_PO_CR, cr & ~AC97_CR_RPBM);
    }
    
    // Set Buffer Descriptor Base Address
    if (g_ac97.mmio_base < 0x10000) {
        // I/O space - use outl for 32-bit
        outl((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_BDBAR, g_ac97.bdl_phys);
    } else {
        mmio_write32(g_ac97.mmio_base + AC97_BMR_PO_BDBAR, g_ac97.bdl_phys);
    }
    
    // Set Last Valid Index (0 = use first buffer descriptor)
    if (g_ac97.mmio_base < 0x10000) {
        outb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_LVI, 0);
    } else {
        mmio_write8(g_ac97.mmio_base + AC97_BMR_PO_LVI, 0);
    }
    
    // Clear status register
    if (g_ac97.mmio_base < 0x10000) {
        outw((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_SR, 0xFFFF);
    } else {
        mmio_write16(g_ac97.mmio_base + AC97_BMR_PO_SR, 0xFFFF);
    }
    
    // Start playback: Set RPBM bit in control register
    if (g_ac97.mmio_base < 0x10000) {
        outb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_CR, AC97_CR_RPBM);
    } else {
        uint8_t cr = mmio_read8(g_ac97.mmio_base + AC97_BMR_PO_CR);
        mmio_write8(g_ac97.mmio_base + AC97_BMR_PO_CR, cr | AC97_CR_RPBM);
    }
    
    g_ac97.playing = 1;
    
    // Wait for playback to complete (approximate)
    uint32_t duration_ms = (sample_count * 1000) / sample_rate;
    terminal_writestring("AC97: Playing ");
    // Print duration
    char dur_str[16];
    int pos = 0;
    uint32_t temp = duration_ms;
    if (temp == 0) {
        dur_str[pos++] = '0';
    } else {
        char rev[16];
        int rev_pos = 0;
        while (temp > 0) {
            rev[rev_pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        for (int j = rev_pos - 1; j >= 0; j--) {
            dur_str[pos++] = rev[j];
        }
    }
    dur_str[pos] = '\0';
    terminal_writestring(dur_str);
    terminal_writestring("ms of audio...\n");
    
    // Wait for playback
    terminal_writestring("AC97: Waiting for playback to complete...\n");
    pit_sleep_ms(duration_ms + 100);  // Add 100ms buffer
    terminal_writestring("AC97: Sleep completed, stopping playback...\n");
    
    // Stop playback
    ac97_stop();
    terminal_writestring("AC97: Playback stopped, returning...\n");
    
    // Free BDL
    if (g_ac97.bdl != NULL) {
        free(g_ac97.bdl);
        g_ac97.bdl = NULL;
    }
    
    return 0;
}

// Stop playback
int ac97_stop(void) {
    if (!g_ac97.initialized || g_ac97.mmio_base == 0) {
        return -1;
    }
    
    // Clear RPBM bit to stop playback
    if (g_ac97.mmio_base < 0x10000) {
        uint8_t cr = inb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_CR);
        outb((uint16_t)g_ac97.mmio_base + AC97_BMR_PO_CR, cr & ~AC97_CR_RPBM);
    } else {
        uint8_t cr = mmio_read8(g_ac97.mmio_base + AC97_BMR_PO_CR);
        mmio_write8(g_ac97.mmio_base + AC97_BMR_PO_CR, cr & ~AC97_CR_RPBM);
    }
    
    g_ac97.playing = 0;
    return 0;
}

// Check if playing
int ac97_is_playing(void) {
    return g_ac97.playing;
}

