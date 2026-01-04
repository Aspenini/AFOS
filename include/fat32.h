#ifndef FAT32_H
#define FAT32_H

#include "types.h"

// FAT32 structures and functions

// FAT32 Boot Sector (BPB - BIOS Parameter Block)
typedef struct __attribute__((packed)) {
    uint8_t jmp[3];              // Jump instruction
    uint8_t oem_name[8];        // OEM name
    uint16_t bytes_per_sector;   // Bytes per sector (usually 512)
    uint8_t sectors_per_cluster; // Sectors per cluster
    uint16_t reserved_sectors;  // Reserved sectors
    uint8_t num_fats;           // Number of FATs
    uint16_t root_entries;      // Root directory entries (FAT12/16 only, 0 for FAT32)
    uint16_t total_sectors_16;  // Total sectors (16-bit, 0 if > 65535)
    uint8_t media_type;         // Media descriptor
    uint16_t sectors_per_fat16; // Sectors per FAT (FAT12/16 only, 0 for FAT32)
    uint16_t sectors_per_track; // Sectors per track
    uint16_t num_heads;         // Number of heads
    uint32_t hidden_sectors;    // Hidden sectors
    uint32_t total_sectors_32;  // Total sectors (32-bit)
    
    // FAT32 Extended Boot Record
    uint32_t sectors_per_fat32; // Sectors per FAT (FAT32)
    uint16_t flags;             // Flags
    uint16_t version;           // FAT32 version
    uint32_t root_cluster;      // Root directory cluster
    uint16_t fs_info_sector;    // FSInfo sector
    uint16_t backup_boot_sector; // Backup boot sector
    uint8_t reserved[12];       // Reserved
    uint8_t drive_number;       // Drive number
    uint8_t reserved1;          // Reserved
    uint8_t boot_signature;     // Boot signature (0x29)
    uint32_t volume_id;         // Volume ID
    uint8_t volume_label[11];   // Volume label
    uint8_t fs_type[8];         // File system type ("FAT32   ")
    uint8_t boot_code[420];     // Boot code
    uint16_t boot_signature2;   // Boot signature (0xAA55)
} fat32_boot_sector_t;

// FAT32 Directory Entry
typedef struct __attribute__((packed)) {
    uint8_t name[11];           // 8.3 filename (space-padded)
    uint8_t attributes;         // File attributes
    uint8_t reserved;           // Reserved
    uint8_t create_time_tenth;  // Creation time (tenths of second)
    uint16_t create_time;       // Creation time
    uint16_t create_date;       // Creation date
    uint16_t access_date;       // Access date
    uint16_t cluster_high;      // High 16 bits of first cluster
    uint16_t modify_time;       // Modification time
    uint16_t modify_date;       // Modification date
    uint16_t cluster_low;       // Low 16 bits of first cluster
    uint32_t file_size;         // File size in bytes
} fat32_dir_entry_t;

// Directory entry attributes
#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LONG_NAME  0x0F

// Special FAT32 cluster values
#define FAT32_CLUSTER_FREE    0x00000000
#define FAT32_CLUSTER_RESERVED 0x00000001
#define FAT32_CLUSTER_BAD     0x0FFFFFF7
#define FAT32_CLUSTER_EOF     0x0FFFFFF8
#define FAT32_CLUSTER_END     0x0FFFFFFF

// FAT32 filesystem structure
typedef struct {
    fat32_boot_sector_t boot_sector;
    uint32_t fat_start_sector;      // First FAT sector
    uint32_t data_start_sector;      // First data sector
    uint32_t root_dir_cluster;       // Root directory cluster
    uint32_t sectors_per_cluster;    // Sectors per cluster
    uint32_t bytes_per_sector;       // Bytes per sector
    uint32_t fat_size_sectors;       // Size of FAT in sectors
    uint32_t total_clusters;         // Total clusters
    int mounted;                      // Is filesystem mounted?
} fat32_fs_t;

// Initialize FAT32 filesystem
int fat32_init(void);

// Mount FAT32 filesystem from disk
int fat32_mount(fat32_fs_t* fs);

// Read directory entries from a cluster
int fat32_read_dir(fat32_fs_t* fs, uint32_t cluster, fat32_dir_entry_t* entries, uint32_t max_entries);

// Find file in directory
int fat32_find_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry);

// Read file data
int fat32_read_file(fat32_fs_t* fs, fat32_dir_entry_t* entry, uint8_t* buffer, uint32_t size);

// Write file data (create or update)
int fat32_write_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size);

// Read FAT entry
uint32_t fat32_read_fat(fat32_fs_t* fs, uint32_t cluster);

// Write FAT entry
int fat32_write_fat(fat32_fs_t* fs, uint32_t cluster, uint32_t value);

// Allocate a new cluster
uint32_t fat32_allocate_cluster(fat32_fs_t* fs);

// Free a cluster chain
int fat32_free_cluster_chain(fat32_fs_t* fs, uint32_t cluster);

// Get next cluster in chain
uint32_t fat32_get_next_cluster(fat32_fs_t* fs, uint32_t cluster);

// Format disk as FAT32 (creates new filesystem)
int fat32_format(uint32_t total_sectors);

// Get global FAT32 filesystem instance
fat32_fs_t* fat32_get_fs(void);

#endif

