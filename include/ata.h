#ifndef ATA_H
#define ATA_H

#include "types.h"

// ATA/IDE disk driver
// Supports primary IDE controller (ports 0x1F0-0x1F7)

// Initialize ATA driver
int ata_init(void);

// Read sectors from disk
// lba: Logical Block Address (sector number)
// count: Number of sectors to read
// buffer: Buffer to store read data (must be at least count * 512 bytes)
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t* buffer);

// Write sectors to disk
// lba: Logical Block Address (sector number)
// count: Number of sectors to write
// buffer: Buffer containing data to write (must be at least count * 512 bytes)
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t* buffer);

// Get disk size in sectors
uint32_t ata_get_sector_count(void);

#endif

