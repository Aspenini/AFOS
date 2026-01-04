#include "blockdev.h"
#include "ata.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);

// Default block device (ATA disk)
static blockdev_t default_blockdev = {0};
static int blockdev_initialized = 0;

// ATA block device read function
static int ata_blockdev_read(uint32_t block, uint32_t count, uint8_t* buffer) {
    return ata_read_sectors(block, (uint8_t)count, buffer);
}

// ATA block device write function
static int ata_blockdev_write(uint32_t block, uint32_t count, const uint8_t* buffer) {
    return ata_write_sectors(block, (uint8_t)count, buffer);
}

// Initialize block device system
int blockdev_init(void) {
    if (blockdev_initialized) {
        return 0;
    }
    
    // Initialize ATA driver
    if (ata_init() != 0) {
        return -1;
    }
    
    // Set up default block device (ATA)
    default_blockdev.block_count = ata_get_sector_count();
    default_blockdev.read_blocks = ata_blockdev_read;
    default_blockdev.write_blocks = ata_blockdev_write;
    default_blockdev.private_data = NULL;
    
    blockdev_initialized = 1;
    terminal_writestring("Block device system initialized\n");
    return 0;
}

// Register a block device
int blockdev_register(blockdev_t* device) {
    if (device == NULL) {
        return -1;
    }
    
    // For now, just set as default
    default_blockdev = *device;
    return 0;
}

// Get default block device
blockdev_t* blockdev_get_default(void) {
    if (!blockdev_initialized) {
        return NULL;
    }
    return &default_blockdev;
}

// Read blocks from default device
int blockdev_read(uint32_t block, uint32_t count, uint8_t* buffer) {
    if (!blockdev_initialized || buffer == NULL) {
        return -1;
    }
    
    if (block + count > default_blockdev.block_count) {
        return -1;  // Out of bounds
    }
    
    if (default_blockdev.read_blocks) {
        return default_blockdev.read_blocks(block, count, buffer);
    }
    
    return -1;
}

// Write blocks to default device
int blockdev_write(uint32_t block, uint32_t count, const uint8_t* buffer) {
    if (!blockdev_initialized || buffer == NULL) {
        return -1;
    }
    
    if (block + count > default_blockdev.block_count) {
        return -1;  // Out of bounds
    }
    
    if (default_blockdev.write_blocks) {
        return default_blockdev.write_blocks(block, count, buffer);
    }
    
    return -1;
}

// Get block count of default device
uint32_t blockdev_get_block_count(void) {
    if (!blockdev_initialized) {
        return 0;
    }
    return default_blockdev.block_count;
}

