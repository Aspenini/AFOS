#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "types.h"

// Block device abstraction
// Provides a unified interface for reading/writing blocks

#define BLOCK_SIZE 512  // Standard sector size

// Block device structure
typedef struct {
    uint32_t block_count;  // Total number of blocks
    int (*read_blocks)(uint32_t block, uint32_t count, uint8_t* buffer);
    int (*write_blocks)(uint32_t block, uint32_t count, const uint8_t* buffer);
    void* private_data;  // Driver-specific data
} blockdev_t;

// Register a block device
int blockdev_register(blockdev_t* device);

// Get default block device
blockdev_t* blockdev_get_default(void);

// Read blocks from default device
int blockdev_read(uint32_t block, uint32_t count, uint8_t* buffer);

// Write blocks to default device
int blockdev_write(uint32_t block, uint32_t count, const uint8_t* buffer);

// Get block count of default device
uint32_t blockdev_get_block_count(void);

// Initialize block device system
int blockdev_init(void);

#endif

