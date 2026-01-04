#include "fat32.h"
#include "blockdev.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A

// Simple string functions (since we don't have stdlib)
static void* memcpy(void* dest, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

static int memcmp(const void* s1, const void* s2, uint32_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

static uint32_t strlen(const char* s) {
    uint32_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

// Global FAT32 filesystem instance
static fat32_fs_t g_fat32_fs = {0};

// Read sector from disk
static int read_sector(uint32_t sector, uint8_t* buffer) {
    return blockdev_read(sector, 1, buffer);
}

// Write sector to disk
static int write_sector(uint32_t sector, const uint8_t* buffer) {
    return blockdev_write(sector, 1, buffer);
}

// Read cluster from disk
static int read_cluster(fat32_fs_t* fs, uint32_t cluster, uint8_t* buffer) {
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        return -1;
    }
    
    uint32_t sector = fs->data_start_sector + (cluster - 2) * fs->sectors_per_cluster;
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (read_sector(sector + i, buffer + (i * fs->bytes_per_sector)) != 0) {
            return -1;
        }
    }
    return 0;
}

// Write cluster to disk
static int write_cluster(fat32_fs_t* fs, uint32_t cluster, const uint8_t* buffer) {
    if (cluster < 2 || cluster >= fs->total_clusters + 2) {
        return -1;
    }
    
    uint32_t sector = fs->data_start_sector + (cluster - 2) * fs->sectors_per_cluster;
    for (uint32_t i = 0; i < fs->sectors_per_cluster; i++) {
        if (write_sector(sector + i, buffer + (i * fs->bytes_per_sector)) != 0) {
            return -1;
        }
    }
    return 0;
}

// Convert 8.3 filename to regular filename
static void fat32_to_filename(const uint8_t* fat_name, char* filename) {
    int i, j = 0;
    
    // Copy name (8 chars)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        filename[j++] = fat_name[i];
    }
    
    // Add extension if present
    if (fat_name[8] != ' ') {
        filename[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            filename[j++] = fat_name[i];
        }
    }
    
    filename[j] = '\0';
}

// Convert regular filename to 8.3 format
static void filename_to_fat32(const char* filename, uint8_t* fat_name) {
    int i, j = 0;
    int dot_pos = -1;
    
    // Find dot position
    for (i = 0; filename[i] != '\0'; i++) {
        if (filename[i] == '.') {
            dot_pos = i;
            break;
        }
    }
    
    // Clear name
    for (i = 0; i < 11; i++) {
        fat_name[i] = ' ';
    }
    
    // Copy name (up to 8 chars before dot)
    int name_len = (dot_pos >= 0) ? dot_pos : strlen(filename);
    if (name_len > 8) name_len = 8;
    
    for (i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';  // Convert to uppercase
        fat_name[i] = c;
    }
    
    // Copy extension (3 chars after dot)
    if (dot_pos >= 0) {
        int ext_len = 0;
        for (i = dot_pos + 1; filename[i] != '\0' && ext_len < 3; i++) {
            char c = filename[i];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';  // Convert to uppercase
            fat_name[8 + ext_len++] = c;
        }
    }
}

// Mount FAT32 filesystem
int fat32_mount(fat32_fs_t* fs) {
    if (fs == NULL) {
        return -1;
    }
    
    // Read boot sector
    uint8_t boot_sector[512];
    if (read_sector(0, boot_sector) != 0) {
        terminal_writestring_color("FAT32: Failed to read boot sector\n", COLOR_RED);
        return -1;
    }
    
    // Copy boot sector
    memcpy(&fs->boot_sector, boot_sector, sizeof(fat32_boot_sector_t));
    
    // Verify FAT32 signature
    if (boot_sector[510] != 0x55 || boot_sector[511] != 0xAA) {
        terminal_writestring_color("FAT32: Invalid boot signature\n", COLOR_RED);
        return -1;
    }
    
    // Check filesystem type
    if (memcmp(fs->boot_sector.fs_type, "FAT32   ", 8) != 0) {
        terminal_writestring_color("FAT32: Not a FAT32 filesystem\n", COLOR_RED);
        return -1;
    }
    
    // Calculate filesystem parameters
    fs->bytes_per_sector = fs->boot_sector.bytes_per_sector;
    fs->sectors_per_cluster = fs->boot_sector.sectors_per_cluster;
    fs->fat_size_sectors = fs->boot_sector.sectors_per_fat32;
    fs->root_dir_cluster = fs->boot_sector.root_cluster;
    
    // Calculate sector offsets
    fs->fat_start_sector = fs->boot_sector.reserved_sectors;
    fs->data_start_sector = fs->fat_start_sector + (fs->boot_sector.num_fats * fs->fat_size_sectors);
    
    // Calculate total clusters
    uint32_t data_sectors = fs->boot_sector.total_sectors_32 - fs->data_start_sector;
    fs->total_clusters = data_sectors / fs->sectors_per_cluster;
    
    fs->mounted = 1;
    
    terminal_writestring_color("FAT32 filesystem mounted\n", COLOR_GREEN);
    return 0;
}

// Read FAT entry
uint32_t fat32_read_fat(fat32_fs_t* fs, uint32_t cluster) {
    if (!fs->mounted || cluster < 2 || cluster >= fs->total_clusters + 2) {
        return FAT32_CLUSTER_BAD;
    }
    
    // Calculate FAT sector and offset
    uint32_t fat_offset = cluster * 4;  // 4 bytes per FAT32 entry
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_index = fat_offset % fs->bytes_per_sector;
    
    // Read FAT sector
    uint8_t fat_sector_buffer[512];
    if (read_sector(fat_sector, fat_sector_buffer) != 0) {
        return FAT32_CLUSTER_BAD;
    }
    
    // Read FAT entry (32-bit, mask upper 4 bits)
    uint32_t* fat_entry = (uint32_t*)(fat_sector_buffer + fat_index);
    return (*fat_entry) & 0x0FFFFFFF;
}

// Write FAT entry
int fat32_write_fat(fat32_fs_t* fs, uint32_t cluster, uint32_t value) {
    if (!fs->mounted || cluster < 2 || cluster >= fs->total_clusters + 2) {
        return -1;
    }
    
    // Calculate FAT sector and offset
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_sector + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_index = fat_offset % fs->bytes_per_sector;
    
    // Read FAT sector
    uint8_t fat_sector_buffer[512];
    if (read_sector(fat_sector, fat_sector_buffer) != 0) {
        return -1;
    }
    
    // Write FAT entry (mask upper 4 bits)
    uint32_t* fat_entry = (uint32_t*)(fat_sector_buffer + fat_index);
    *fat_entry = value & 0x0FFFFFFF;
    
    // Write back to both FATs (for redundancy)
    if (write_sector(fat_sector, fat_sector_buffer) != 0) {
        return -1;
    }
    
    // Write to second FAT if it exists
    if (fs->boot_sector.num_fats > 1) {
        uint32_t fat2_sector = fat_sector + fs->fat_size_sectors;
        if (write_sector(fat2_sector, fat_sector_buffer) != 0) {
            return -1;
        }
    }
    
    return 0;
}

// Get next cluster in chain
uint32_t fat32_get_next_cluster(fat32_fs_t* fs, uint32_t cluster) {
    return fat32_read_fat(fs, cluster);
}

// Allocate a new cluster
uint32_t fat32_allocate_cluster(fat32_fs_t* fs) {
    // Simple allocation: find first free cluster
    // In a real implementation, you'd use a more sophisticated algorithm
    for (uint32_t cluster = 2; cluster < fs->total_clusters + 2; cluster++) {
        uint32_t fat_entry = fat32_read_fat(fs, cluster);
        if (fat_entry == FAT32_CLUSTER_FREE) {
            // Mark as EOF
            fat32_write_fat(fs, cluster, FAT32_CLUSTER_EOF);
            return cluster;
        }
    }
    return 0;  // No free clusters
}

// Free a cluster chain
int fat32_free_cluster_chain(fat32_fs_t* fs, uint32_t cluster) {
    while (cluster >= 2 && cluster < fs->total_clusters + 2) {
        uint32_t next = fat32_read_fat(fs, cluster);
        fat32_write_fat(fs, cluster, FAT32_CLUSTER_FREE);
        if (next >= FAT32_CLUSTER_EOF) {
            break;  // End of chain
        }
        cluster = next;
    }
    return 0;
}

// Read directory entries
int fat32_read_dir(fat32_fs_t* fs, uint32_t cluster, fat32_dir_entry_t* entries, uint32_t max_entries) {
    if (!fs->mounted || entries == NULL) {
        return -1;
    }
    
    uint32_t entries_read = 0;
    uint8_t cluster_buffer[512 * 16];  // Assume max 16 sectors per cluster
    
    // Read cluster chain
    while (cluster >= 2 && cluster < fs->total_clusters + 2 && entries_read < max_entries) {
        if (read_cluster(fs, cluster, cluster_buffer) != 0) {
            break;
        }
        
        // Parse directory entries (16 entries per sector for 32-byte entries)
        uint32_t entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
        uint32_t total_entries_in_cluster = entries_per_sector * fs->sectors_per_cluster;
        
        fat32_dir_entry_t* dir_entries = (fat32_dir_entry_t*)cluster_buffer;
        
        for (uint32_t i = 0; i < total_entries_in_cluster && entries_read < max_entries; i++) {
            // Check if entry is valid (not free, not deleted)
            if (dir_entries[i].name[0] == 0x00) {
                // End of directory
                return entries_read;
            }
            if (dir_entries[i].name[0] == 0xE5) {
                // Deleted entry, skip
                continue;
            }
            if (dir_entries[i].attributes == FAT32_ATTR_LONG_NAME) {
                // Long filename entry, skip for now
                continue;
            }
            
            // Copy entry
            memcpy(&entries[entries_read], &dir_entries[i], sizeof(fat32_dir_entry_t));
            entries_read++;
        }
        
        // Get next cluster
        cluster = fat32_get_next_cluster(fs, cluster);
        if (cluster >= FAT32_CLUSTER_EOF) {
            break;
        }
    }
    
    return entries_read;
}

// Find file in directory
int fat32_find_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry) {
    if (!fs->mounted || filename == NULL || entry == NULL) {
        return -1;
    }
    
    // Convert filename to 8.3 format
    uint8_t fat_name[11];
    filename_to_fat32(filename, fat_name);
    
    // Read directory
    fat32_dir_entry_t entries[64];
    int count = fat32_read_dir(fs, dir_cluster, entries, 64);
    
    // Search for file
    for (int i = 0; i < count; i++) {
        if (memcmp(entries[i].name, fat_name, 11) == 0) {
            memcpy(entry, &entries[i], sizeof(fat32_dir_entry_t));
            return 0;
        }
    }
    
    return -1;  // Not found
}

// Read file data
int fat32_read_file(fat32_fs_t* fs, fat32_dir_entry_t* entry, uint8_t* buffer, uint32_t size) {
    if (!fs->mounted || entry == NULL || buffer == NULL) {
        return -1;
    }
    
    // Get first cluster
    uint32_t cluster = entry->cluster_low | (entry->cluster_high << 16);
    uint32_t bytes_read = 0;
    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t cluster_buffer[512 * 16];  // Max cluster size
    
    while (cluster >= 2 && cluster < fs->total_clusters + 2 && bytes_read < size) {
        if (read_cluster(fs, cluster, cluster_buffer) != 0) {
            return -1;
        }
        
        uint32_t to_copy = size - bytes_read;
        if (to_copy > cluster_size) {
            to_copy = cluster_size;
        }
        
        memcpy(buffer + bytes_read, cluster_buffer, to_copy);
        bytes_read += to_copy;
        
        // Get next cluster
        cluster = fat32_get_next_cluster(fs, cluster);
        if (cluster >= FAT32_CLUSTER_EOF) {
            break;
        }
    }
    
    return bytes_read;
}

// Find free directory entry in a cluster
static int find_free_dir_entry(fat32_fs_t* fs, uint32_t dir_cluster, uint32_t* out_cluster, uint32_t* out_index) {
    uint8_t cluster_buffer[512 * 16];
    uint32_t current_cluster = dir_cluster;
    uint32_t entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
    
    while (current_cluster >= 2 && current_cluster < fs->total_clusters + 2) {
        if (read_cluster(fs, current_cluster, cluster_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)cluster_buffer;
        uint32_t total_entries = entries_per_sector * fs->sectors_per_cluster;
        
        for (uint32_t i = 0; i < total_entries; i++) {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
                // Free or deleted entry
                *out_cluster = current_cluster;
                *out_index = i;
                return 0;
            }
        }
        
        // Check if we need to extend directory
        uint32_t next = fat32_get_next_cluster(fs, current_cluster);
        if (next >= FAT32_CLUSTER_EOF) {
            // Allocate new cluster for directory
            uint32_t new_cluster = fat32_allocate_cluster(fs);
            if (new_cluster == 0) {
                return -1;  // No free clusters
            }
            fat32_write_fat(fs, current_cluster, new_cluster);
            fat32_write_fat(fs, new_cluster, FAT32_CLUSTER_EOF);
            
            // Clear new cluster
            uint8_t zero_buffer[512 * 16] = {0};
            write_cluster(fs, new_cluster, zero_buffer);
            
            *out_cluster = new_cluster;
            *out_index = 0;
            return 0;
        }
        
        current_cluster = next;
    }
    
    return -1;
}

// Write file data (creates new file or overwrites existing)
int fat32_write_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size) {
    if (!fs->mounted || filename == NULL) {
        return -1;
    }
    // Allow NULL data for empty files (size will be 0)
    if (data == NULL && size > 0) {
        return -1;
    }
    // For empty files, use empty string
    if (data == NULL) {
        data = (uint8_t*)"";
    }
    
    // Check if file already exists
    fat32_dir_entry_t existing_entry;
    int file_exists = (fat32_find_file(fs, dir_cluster, filename, &existing_entry) == 0);
    
    uint32_t first_cluster;
    if (file_exists) {
        // Free existing cluster chain
        first_cluster = existing_entry.cluster_low | (existing_entry.cluster_high << 16);
        fat32_free_cluster_chain(fs, first_cluster);
    } else {
        first_cluster = 0;
    }
    
    // Allocate clusters for file data
    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;
    
    if (clusters_needed == 0) {
        clusters_needed = 1;  // At least one cluster
    }
    
    // Allocate cluster chain array (max clusters needed)
    static uint32_t cluster_chain[1024];  // Max 1024 clusters (512KB per cluster = 512MB max file)
    if (clusters_needed > 1024) {
        return -1;  // File too large
    }
    
    uint32_t prev_cluster = 0;
    
    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint32_t cluster = fat32_allocate_cluster(fs);
        if (cluster == 0) {
            // Free allocated clusters on error
            if (prev_cluster != 0) {
                fat32_free_cluster_chain(fs, cluster_chain[0]);
            }
            return -1;
        }
        
        cluster_chain[i] = cluster;
        
        if (prev_cluster != 0) {
            fat32_write_fat(fs, prev_cluster, cluster);
        } else {
            first_cluster = cluster;
        }
        
        prev_cluster = cluster;
    }
    
    // Mark last cluster as EOF
    fat32_write_fat(fs, prev_cluster, FAT32_CLUSTER_EOF);
    
    // Write file data to clusters
    uint32_t bytes_written = 0;
    uint8_t write_buffer[512 * 16];
    
    for (uint32_t i = 0; i < clusters_needed && bytes_written < size; i++) {
        uint32_t to_write = size - bytes_written;
        if (to_write > cluster_size) {
            to_write = cluster_size;
        }
        
        // Copy data to cluster buffer
        for (uint32_t j = 0; j < to_write; j++) {
            write_buffer[j] = data[bytes_written + j];
        }
        // Zero out rest of cluster
        for (uint32_t j = to_write; j < cluster_size; j++) {
            write_buffer[j] = 0;
        }
        
        if (write_cluster(fs, cluster_chain[i], write_buffer) != 0) {
            // Error writing, free clusters
            fat32_free_cluster_chain(fs, first_cluster);
            return -1;
        }
        
        bytes_written += to_write;
    }
    
    // Find or create directory entry
    uint32_t entry_cluster, entry_index;
    if (file_exists) {
        // Update existing entry - need to find it
        // For simplicity, we'll search for it
        uint8_t dir_buffer[512 * 16];
        uint32_t search_cluster = dir_cluster;
        uint32_t entries_per_sector = fs->bytes_per_sector / sizeof(fat32_dir_entry_t);
        uint8_t fat_name[11];
        filename_to_fat32(filename, fat_name);
        
        while (search_cluster >= 2 && search_cluster < fs->total_clusters + 2) {
            if (read_cluster(fs, search_cluster, dir_buffer) != 0) {
                break;
            }
            
            fat32_dir_entry_t* entries = (fat32_dir_entry_t*)dir_buffer;
            uint32_t total_entries = entries_per_sector * fs->sectors_per_cluster;
            
            for (uint32_t i = 0; i < total_entries; i++) {
                if (memcmp(entries[i].name, fat_name, 11) == 0) {
                    entry_cluster = search_cluster;
                    entry_index = i;
                    goto found_entry;
                }
            }
            
            search_cluster = fat32_get_next_cluster(fs, search_cluster);
            if (search_cluster >= FAT32_CLUSTER_EOF) {
                break;
            }
        }
        return -1;  // Couldn't find existing entry
        
        found_entry:
        // Update existing entry
        if (read_cluster(fs, entry_cluster, dir_buffer) != 0) {
            return -1;
        }
        
        fat32_dir_entry_t* entry = (fat32_dir_entry_t*)dir_buffer + entry_index;
        entry->cluster_low = first_cluster & 0xFFFF;
        entry->cluster_high = (first_cluster >> 16) & 0xFFFF;
        entry->file_size = size;
        
        if (write_cluster(fs, entry_cluster, dir_buffer) != 0) {
            return -1;
        }
    } else {
        // Create new entry
        if (find_free_dir_entry(fs, dir_cluster, &entry_cluster, &entry_index) != 0) {
            fat32_free_cluster_chain(fs, first_cluster);
            return -1;
        }
        
        uint8_t dir_buffer[512 * 16];
        if (read_cluster(fs, entry_cluster, dir_buffer) != 0) {
            fat32_free_cluster_chain(fs, first_cluster);
            return -1;
        }
        
        fat32_dir_entry_t* entry = (fat32_dir_entry_t*)dir_buffer + entry_index;
        
        // Clear entry
        for (uint32_t i = 0; i < sizeof(fat32_dir_entry_t); i++) {
            ((uint8_t*)entry)[i] = 0;
        }
        
        // Set filename
        filename_to_fat32(filename, entry->name);
        
        // Set attributes (regular file)
        entry->attributes = FAT32_ATTR_ARCHIVE;
        
        // Set cluster
        entry->cluster_low = first_cluster & 0xFFFF;
        entry->cluster_high = (first_cluster >> 16) & 0xFFFF;
        
        // Set file size
        entry->file_size = size;
        
        // Set date/time (simplified - use fixed values)
        entry->create_date = 0x4A00;  // 2024-01-01
        entry->modify_date = 0x4A00;
        entry->access_date = 0x4A00;
        
        if (write_cluster(fs, entry_cluster, dir_buffer) != 0) {
            fat32_free_cluster_chain(fs, first_cluster);
            return -1;
        }
    }
    
    return size;
}

// Initialize FAT32
int fat32_init(void) {
    // Try to mount existing filesystem
    if (fat32_mount(&g_fat32_fs) == 0) {
        return 0;
    }
    
    // If mount failed, filesystem doesn't exist
    terminal_writestring_color("FAT32: No filesystem found on disk\n", COLOR_RED);
    return -1;
}

// Get global FAT32 filesystem instance
fat32_fs_t* fat32_get_fs(void) {
    return &g_fat32_fs;
}

// Format disk as FAT32 (simplified - creates basic structure)
int fat32_format(uint32_t total_sectors) {
    // TODO: Implement FAT32 formatting
    // This requires:
    // 1. Writing boot sector
    // 2. Initializing FAT tables
    // 3. Creating root directory
    // 4. Writing FSInfo sector
    
    terminal_writestring_color("FAT32: Formatting not yet implemented\n", COLOR_RED);
    return -1;
}

