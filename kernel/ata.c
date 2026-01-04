#include "ata.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C

// ATA/IDE ports (primary controller)
#define ATA_DATA_PORT        0x1F0
#define ATA_ERROR_PORT       0x1F1
#define ATA_SECTOR_COUNT     0x1F2
#define ATA_LBA_LOW          0x1F3
#define ATA_LBA_MID          0x1F4
#define ATA_LBA_HIGH         0x1F5
#define ATA_DEVICE_PORT      0x1F6
#define ATA_COMMAND_PORT     0x1F7
#define ATA_STATUS_PORT      0x1F7
#define ATA_ALT_STATUS       0x3F6

// ATA commands
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC

// ATA status bits
#define ATA_STATUS_ERR   0x01
#define ATA_STATUS_DRQ   0x08  // Data Request
#define ATA_STATUS_BSY   0x80  // Busy

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

// Read 16-bit word from I/O port
static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("inw %1, %0" : "=a"(result) : "Nd"(port));
    return result;
}

// Write 16-bit word to I/O port
static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

// Wait for ATA controller to be ready
static int ata_wait_ready(void) {
    uint8_t status;
    int timeout = 100000;  // Timeout counter
    
    while (timeout-- > 0) {
        status = inb(ATA_STATUS_PORT);
        if (!(status & ATA_STATUS_BSY)) {
            if (status & ATA_STATUS_ERR) {
                return -1;  // Error occurred
            }
            return 0;  // Ready
        }
        // Small delay
        for (volatile int i = 0; i < 100; i++);
    }
    return -1;  // Timeout
}

// Wait for data request
static int ata_wait_drq(void) {
    uint8_t status;
    int timeout = 100000;
    
    while (timeout-- > 0) {
        status = inb(ATA_STATUS_PORT);
        if (status & ATA_STATUS_DRQ) {
            return 0;  // Data ready
        }
        if (status & ATA_STATUS_ERR) {
            return -1;  // Error
        }
        for (volatile int i = 0; i < 100; i++);
    }
    return -1;  // Timeout
}

// Initialize ATA driver
int ata_init(void) {
    // Select master drive (bit 4 = 0)
    outb(ATA_DEVICE_PORT, 0xE0);  // LBA mode, master drive
    
    // Wait for controller to be ready
    if (ata_wait_ready() != 0) {
        terminal_writestring_color("ATA: Controller not ready\n", COLOR_RED);
        return -1;
    }
    
    // Send IDENTIFY command
    outb(ATA_COMMAND_PORT, ATA_CMD_IDENTIFY);
    
    // Wait a bit
    for (volatile int i = 0; i < 1000; i++);
    
    // Check if device exists
    uint8_t status = inb(ATA_STATUS_PORT);
    if (status == 0) {
        terminal_writestring_color("ATA: No device found\n", COLOR_RED);
        return -1;
    }
    
    // Wait for data ready
    if (ata_wait_drq() != 0) {
        terminal_writestring_color("ATA: Device not responding\n", COLOR_RED);
        return -1;
    }
    
    // Read IDENTIFY data (we'll just discard it for now)
    for (int i = 0; i < 256; i++) {
        inw(ATA_DATA_PORT);
    }
    
    terminal_writestring("ATA disk driver initialized\n");
    return 0;
}

// Read sectors from disk
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer) {
    if (buffer == NULL || count == 0) {
        return -1;
    }
    
    // Wait for controller to be ready
    if (ata_wait_ready() != 0) {
        return -1;
    }
    
    // Select master drive and set LBA mode
    outb(ATA_DEVICE_PORT, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Set sector count
    outb(ATA_SECTOR_COUNT, count);
    
    // Set LBA address
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send read command
    outb(ATA_COMMAND_PORT, ATA_CMD_READ_SECTORS);
    
    // Read sectors
    for (uint8_t sector = 0; sector < count; sector++) {
        // Wait for data ready
        if (ata_wait_drq() != 0) {
            return -1;
        }
        
        // Read 256 words (512 bytes = 1 sector)
        uint16_t* sector_buffer = (uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            sector_buffer[i] = inw(ATA_DATA_PORT);
        }
    }
    
    return 0;
}

// Write sectors to disk
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer) {
    if (buffer == NULL || count == 0) {
        return -1;
    }
    
    // Wait for controller to be ready
    if (ata_wait_ready() != 0) {
        return -1;
    }
    
    // Select master drive and set LBA mode
    outb(ATA_DEVICE_PORT, 0xE0 | ((lba >> 24) & 0x0F));
    
    // Set sector count
    outb(ATA_SECTOR_COUNT, count);
    
    // Set LBA address
    outb(ATA_LBA_LOW, lba & 0xFF);
    outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send write command
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);
    
    // Write sectors
    for (uint8_t sector = 0; sector < count; sector++) {
        // Wait for data ready
        if (ata_wait_drq() != 0) {
            return -1;
        }
        
        // Write 256 words (512 bytes = 1 sector)
        const uint16_t* sector_buffer = (const uint16_t*)(buffer + (sector * 512));
        for (int i = 0; i < 256; i++) {
            outw(ATA_DATA_PORT, sector_buffer[i]);
        }
        
        // Wait for write to complete
        if (ata_wait_ready() != 0) {
            return -1;
        }
    }
    
    // Flush cache
    outb(ATA_COMMAND_PORT, 0xE7);  // FLUSH CACHE command
    ata_wait_ready();
    
    return 0;
}

// Get disk size in sectors (simplified - assumes 512 bytes per sector)
// In a real implementation, this would read IDENTIFY data
uint32_t ata_get_sector_count(void) {
    // For now, return a default size (e.g., 100MB = 204800 sectors)
    // In a real implementation, read from IDENTIFY data
    return 204800;  // 100MB default
}

