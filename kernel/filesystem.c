#include "filesystem.h"
#include "types.h"
#include "fat32.h"

fs_node_t* fs_root = NULL;
fs_node_t* fs_current_dir = NULL;

// Simple file data storage (in-memory)
#define FILE_DATA_POOL_SIZE (512 * 1024)  // 512KB for file data
static uint8_t file_data_pool[FILE_DATA_POOL_SIZE];
static uint32_t file_data_used = 0;

// Create a new filesystem node
fs_node_t* fs_create_node(const char* name, fs_node_type_t type, fs_node_t* parent) {
    fs_node_t* node = NULL;
    
    // Simple allocation - in a real OS, you'd use a proper memory allocator
    // For now, we'll use static allocation
    static fs_node_t nodes[256];
    static uint32_t node_count = 0;
    
    if (node_count >= 256) {
        return NULL;
    }
    
    node = &nodes[node_count++];
    
    // Copy name
    uint32_t i = 0;
    while (name[i] != '\0' && i < MAX_FILENAME_LENGTH - 1) {
        node->name[i] = name[i];
        i++;
    }
    node->name[i] = '\0';
    
    node->type = type;
    node->parent = parent;
    node->child_count = 0;
    node->data = NULL;
    node->data_size = 0;
    
    // Initialize children array
    for (i = 0; i < MAX_DIR_ENTRIES; i++) {
        node->children[i] = NULL;
    }
    
    // Add to parent if parent exists
    if (parent != NULL && parent->child_count < MAX_DIR_ENTRIES) {
        parent->children[parent->child_count++] = node;
    }
    
    return node;
}

// Find a child node by name
fs_node_t* fs_find_child(fs_node_t* dir, const char* name) {
    if (dir == NULL || dir->type != FS_DIRECTORY) {
        return NULL;
    }
    
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (dir->children[i] != NULL) {
            uint32_t j = 0;
            int match = 1;
            while (name[j] != '\0' && dir->children[i]->name[j] != '\0') {
                if (name[j] != dir->children[i]->name[j]) {
                    match = 0;
                    break;
                }
                j++;
            }
            if (match && name[j] == '\0' && dir->children[i]->name[j] == '\0') {
                return dir->children[i];
            }
        }
    }
    
    return NULL;
}

// Resolve a path (supports absolute and relative paths)
fs_node_t* fs_resolve_path(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return fs_current_dir;
    }
    
    fs_node_t* current = fs_current_dir;
    
    // Handle absolute path
    if (path[0] == '/') {
        current = fs_root;
        path++;
    }
    
    // Handle ".." (parent directory)
    if (path[0] == '.' && path[1] == '.' && (path[2] == '/' || path[2] == '\0')) {
        if (current->parent != NULL) {
            current = current->parent;
        }
        if (path[2] == '/') {
            path += 3;
        } else {
            return current;
        }
    }
    
    // Handle "." (current directory)
    if (path[0] == '.' && (path[1] == '/' || path[1] == '\0')) {
        if (path[1] == '/') {
            path += 2;
        } else {
            return current;
        }
    }
    
    // Parse path components
    while (path[0] != '\0') {
        // Find next component
        uint32_t len = 0;
        while (path[len] != '/' && path[len] != '\0') {
            len++;
        }
        
        if (len == 0) {
            path++;
            continue;
        }
        
        // Extract component name
        char component[MAX_FILENAME_LENGTH];
        uint32_t i;
        for (i = 0; i < len && i < MAX_FILENAME_LENGTH - 1; i++) {
            component[i] = path[i];
        }
        component[i] = '\0';
        
        // Find child
        fs_node_t* child = fs_find_child(current, component);
        if (child == NULL) {
            return NULL;
        }
        
        current = child;
        path += len;
        
        if (path[0] == '/') {
            path++;
        }
    }
    
    return current;
}

// Create a directory
int fs_mkdir(fs_node_t* parent, const char* name) {
    if (parent == NULL || parent->type != FS_DIRECTORY) {
        return -1;
    }
    
    // Check if already exists
    if (fs_find_child(parent, name) != NULL) {
        return -1;
    }
    
    // Create new directory
    fs_node_t* dir = fs_create_node(name, FS_DIRECTORY, parent);
    if (dir == NULL) {
        return -1;
    }
    
    return 0;
}

// List directory contents
void fs_list_directory(fs_node_t* dir) {
    if (dir == NULL || dir->type != FS_DIRECTORY) {
        return;
    }
    
    // In shell.c, we'll format this output
}

// Find program in /sys/components by name (strips extension)
// Returns NULL if not found or if multiple matches (duplicate names with different extensions)
fs_node_t* fs_find_program(const char* name) {
    if (name == NULL || fs_root == NULL) {
        return NULL;
    }
    
    // Forward declaration for terminal functions
    extern void terminal_writestring_color(const char* data, uint8_t color);
    #define COLOR_RED 0x0C
    
    // Navigate to /sys/components
    fs_node_t* sys = fs_find_child(fs_root, "sys");
    if (sys == NULL) {
        return NULL;
    }
    
    fs_node_t* components = fs_find_child(sys, "components");
    if (components == NULL) {
        return NULL;
    }
    
    // Count how many matches we find
    uint32_t match_count = 0;
    fs_node_t* first_match = NULL;
    
    // Check exact match first
    fs_node_t* found = fs_find_child(components, name);
    if (found != NULL) {
        match_count++;
        first_match = found;
    }
    
    // Try with common extensions (including .bas for BASIC programs and .bf for Brainfuck)
    const char* extensions[] = {".afos", ".bas", ".bf", ".exe", ".bin", ".app", NULL};
    for (int i = 0; extensions[i] != NULL; i++) {
        char fullname[MAX_FILENAME_LENGTH];
        uint32_t j = 0;
        while (name[j] != '\0' && j < MAX_FILENAME_LENGTH - 10) {
            fullname[j] = name[j];
            j++;
        }
        uint32_t ext_idx = 0;
        while (extensions[i][ext_idx] != '\0' && j < MAX_FILENAME_LENGTH - 1) {
            fullname[j] = extensions[i][ext_idx];
            j++;
            ext_idx++;
        }
        fullname[j] = '\0';
        
        found = fs_find_child(components, fullname);
        if (found != NULL) {
            match_count++;
            if (first_match == NULL) {
                first_match = found;
            }
        }
    }
    
    // If multiple matches found, show error
    if (match_count > 1) {
        terminal_writestring_color("Error: Multiple programs with the same name found in /sys/components\n", COLOR_RED);
        return NULL;
    }
    
    return first_match;
}

// Initialize filesystem
void fs_init(void) {
    // Create root directory
    fs_root = fs_create_node("/", FS_DIRECTORY, NULL);
    fs_current_dir = fs_root;
    
    // Create system directory structure
    fs_mkdir(fs_root, "sys");
    fs_mkdir(fs_root, "home");
    
    // Create /sys/components for system programs
    fs_node_t* sys = fs_find_child(fs_root, "sys");
    if (sys != NULL) {
        fs_mkdir(sys, "components");
    }
    
    // Create some files in home
    fs_node_t* home = fs_find_child(fs_root, "home");
    if (home != NULL) {
        fs_create_node("readme.txt", FS_FILE, home);
    }
}

// Create a file with data
int fs_create_file(fs_node_t* parent, const char* name, const uint8_t* data, uint32_t size) {
    if (parent == NULL || parent->type != FS_DIRECTORY) {
        return -1;
    }
    
    // Check if already exists
    if (fs_find_child(parent, name) != NULL) {
        return -1;
    }
    
    // Create file node
    fs_node_t* file = fs_create_node(name, FS_FILE, parent);
    if (file == NULL) {
        return -1;
    }
    
    // Handle data allocation
    if (size == 0) {
        // Empty file - no data allocation needed
        file->data = NULL;
        file->data_size = 0;
    } else if (data == NULL) {
        // Disk-only file (data not loaded into memory, but size is known)
        file->data = NULL;
        file->data_size = size;  // Store size so we know it exists on disk
    } else {
        // Check if we have enough space to load into memory
        if (file_data_used + size <= FILE_DATA_POOL_SIZE) {
            // Allocate and copy data
            file->data = &file_data_pool[file_data_used];
            file->data_size = size;
            file_data_used += size;
            
            // Copy data
            for (uint32_t i = 0; i < size; i++) {
                file->data[i] = data[i];
            }
        } else {
            // File too large - create entry but don't load data (will read from disk)
            file->data = NULL;
            file->data_size = size;  // Store size so we know it exists
        }
    }
    
    return 0;
}

// Read file data
int fs_read_file(fs_node_t* file, uint8_t* buffer, uint32_t size) {
    if (file == NULL || file->type != FS_FILE || buffer == NULL) {
        return -1;
    }
    
    // If data is in memory, read from memory
    if (file->data != NULL) {
        uint32_t to_copy = size < file->data_size ? size : file->data_size;
        for (uint32_t i = 0; i < to_copy; i++) {
            buffer[i] = file->data[i];
        }
        return to_copy;
    }
    
    // Data is NULL but file_size > 0 means it's on disk - read from disk
    if (file->data_size > 0) {
        extern fat32_fs_t* fat32_get_fs(void);
        extern int fat32_find_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry);
        extern int fat32_read_file(fat32_fs_t* fs, fat32_dir_entry_t* entry, uint8_t* buffer, uint32_t size);
        
        fat32_fs_t* fs = fat32_get_fs();
        if (fs != NULL && fs->mounted) {
            // Find the file on disk
            // We need to find which directory this file is in
            // For now, try home directory
            fat32_dir_entry_t entry;
            if (fat32_find_file(fs, fs->root_dir_cluster, "HOME", &entry) == 0 && (entry.attributes & 0x10)) {
                uint32_t home_cluster = entry.cluster_low | (entry.cluster_high << 16);
                if (fat32_find_file(fs, home_cluster, file->name, &entry) == 0) {
                    uint32_t to_read = size < file->data_size ? size : file->data_size;
                    return fat32_read_file(fs, &entry, buffer, to_read);
                }
            } else if (fat32_find_file(fs, fs->root_dir_cluster, "home", &entry) == 0 && (entry.attributes & 0x10)) {
                uint32_t home_cluster = entry.cluster_low | (entry.cluster_high << 16);
                if (fat32_find_file(fs, home_cluster, file->name, &entry) == 0) {
                    uint32_t to_read = size < file->data_size ? size : file->data_size;
                    return fat32_read_file(fs, &entry, buffer, to_read);
                }
            }
        }
    }
    
    return 0;  // No data available
}

// Read file data with offset
int fs_read_file_at(fs_node_t* file, uint32_t offset, uint8_t* buffer, uint32_t size) {
    if (file == NULL || file->type != FS_FILE || buffer == NULL) {
        return -1;
    }
    
    // If data is in memory, read from memory
    if (file->data != NULL) {
        if (offset >= file->data_size) {
            return 0;  // Offset beyond file
        }
        uint32_t to_copy = size;
        if (offset + to_copy > file->data_size) {
            to_copy = file->data_size - offset;
        }
        for (uint32_t i = 0; i < to_copy; i++) {
            buffer[i] = file->data[offset + i];
        }
        return to_copy;
    }
    
    // Data is NULL but file_size > 0 means it's on disk - read from disk
    if (file->data_size > 0) {
        extern fat32_fs_t* fat32_get_fs(void);
        extern int fat32_find_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry);
        extern int fat32_read_file_at(fat32_fs_t* fs, fat32_dir_entry_t* entry, uint32_t offset, uint8_t* buffer, uint32_t size);
        
        fat32_fs_t* fs = fat32_get_fs();
        if (fs != NULL && fs->mounted) {
            // Find the file on disk
            // We need to find which directory this file is in
            // For now, try home directory
            fat32_dir_entry_t entry;
            if (fat32_find_file(fs, fs->root_dir_cluster, "HOME", &entry) == 0 && (entry.attributes & 0x10)) {
                uint32_t home_cluster = entry.cluster_low | (entry.cluster_high << 16);
                if (fat32_find_file(fs, home_cluster, file->name, &entry) == 0) {
                    uint32_t to_read = size;
                    if (offset + to_read > file->data_size) {
                        to_read = file->data_size - offset;
                    }
                    return fat32_read_file_at(fs, &entry, offset, buffer, to_read);
                }
            } else if (fat32_find_file(fs, fs->root_dir_cluster, "home", &entry) == 0 && (entry.attributes & 0x10)) {
                uint32_t home_cluster = entry.cluster_low | (entry.cluster_high << 16);
                if (fat32_find_file(fs, home_cluster, file->name, &entry) == 0) {
                    uint32_t to_read = size;
                    if (offset + to_read > file->data_size) {
                        to_read = file->data_size - offset;
                    }
                    return fat32_read_file_at(fs, &entry, offset, buffer, to_read);
                }
            }
        }
    }
    
    return 0;  // No data available
}

// Get file size
uint32_t fs_get_file_size(fs_node_t* file) {
    if (file == NULL || file->type != FS_FILE) {
        return 0;
    }
    return file->data_size;
}

// Write to file (extends or overwrites file data)
int fs_write_file(fs_node_t* file, const uint8_t* data, uint32_t size) {
    if (file == NULL || file->type != FS_FILE || data == NULL) {
        return -1;
    }
    
    // Check if we have enough space
    uint32_t new_size = file->data_size + size;
    if (file_data_used - file->data_size + new_size > FILE_DATA_POOL_SIZE) {
        return -1;  // Not enough space
    }
    
    // If file already has data, we need to reallocate
    // For simplicity, we'll extend the file
    if (file->data == NULL) {
        // Allocate new space
        file->data = &file_data_pool[file_data_used];
        file->data_size = size;
        file_data_used += size;
        
        // Copy data
        for (uint32_t i = 0; i < size; i++) {
            file->data[i] = data[i];
        }
    } else {
        // Extend file (simple append)
        uint32_t old_size = file->data_size;
        
        // Move data if needed (simplified - in real OS would use better allocation)
        // For now, just append if there's space
        if (file_data_used + size <= FILE_DATA_POOL_SIZE) {
            // Append to end
            uint8_t* new_data = &file_data_pool[file_data_used];
            for (uint32_t i = 0; i < old_size; i++) {
                new_data[i] = file->data[i];
            }
            for (uint32_t i = 0; i < size; i++) {
                new_data[old_size + i] = data[i];
            }
            file->data = new_data;
            file->data_size = old_size + size;
            file_data_used += size;
        } else {
            return -1;  // Not enough space
        }
    }
    
    return size;
}

// Save filesystem to disk (saves in-memory files to FAT32)
int fs_save_to_disk(void) {
    extern fat32_fs_t* fat32_get_fs(void);
    extern int fat32_write_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, const uint8_t* data, uint32_t size);
    
    fat32_fs_t* fs = fat32_get_fs();
    
    if (fs == NULL || !fs->mounted) {
        return -1;
    }
    
    // Save files from /home directory to FAT32 root
    fs_node_t* home = fs_find_child(fs_root, "home");
    if (home == NULL) {
        return 0;  // No home directory
    }
    
    // Iterate through files in /home and save them to disk
    for (uint32_t i = 0; i < home->child_count; i++) {
        fs_node_t* file = home->children[i];
        if (file != NULL && file->type == FS_FILE) {
            // Write file to FAT32 (even if empty - data can be NULL, size can be 0)
            uint8_t* data = (file->data != NULL) ? file->data : (uint8_t*)"";
            uint32_t size = file->data_size;
            
            // Debug: print what we're saving
            extern void terminal_writestring(const char*);
            extern void terminal_writestring_color(const char*, uint8_t);
            terminal_writestring("Saving file: ");
            terminal_writestring(file->name);
            terminal_writestring(" (size: ");
            // Simple number to string conversion for size
            char size_str[16];
            uint32_t temp = size;
            int pos = 0;
            if (temp == 0) {
                size_str[pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (temp > 0) {
                    rev[rev_pos++] = '0' + (temp % 10);
                    temp /= 10;
                }
                for (int j = rev_pos - 1; j >= 0; j--) {
                    size_str[pos++] = rev[j];
                }
            }
            size_str[pos] = '\0';
            terminal_writestring(size_str);
            terminal_writestring(")\n");
            
            if (fat32_write_file(fs, fs->root_dir_cluster, file->name, data, size) < 0) {
                // Error writing file, but continue with others
                terminal_writestring_color("Error: Failed to write file to disk\n", 0x0C); // Red
            } else {
                terminal_writestring_color("Successfully saved to disk\n", 0x0A); // Green
            }
        }
    }
    
    return 0;
}

// Helper function to load files from a directory
static void load_files_from_dir(fat32_fs_t* fs, uint32_t dir_cluster, const char* dir_name) {
    extern void terminal_writestring(const char*);
    extern void terminal_writestring_color(const char*, uint8_t);
    extern int fat32_read_dir(fat32_fs_t* fs, uint32_t dir_cluster, fat32_dir_entry_t* entries, uint32_t max_entries);
    extern int fat32_read_file(fat32_fs_t* fs, fat32_dir_entry_t* entry, uint8_t* buffer, uint32_t size);
    fat32_dir_entry_t entries[64];
    int count = fat32_read_dir(fs, dir_cluster, entries, 64);
    
    if (count < 0) {
        return;
    }
    
    terminal_writestring("Found ");
    // Simple number to string
    char count_str[16];
    int temp = count;
    int pos = 0;
    if (temp == 0) {
        count_str[pos++] = '0';
    } else {
        char rev[16];
        int rev_pos = 0;
        while (temp > 0) {
            rev[rev_pos++] = '0' + (temp % 10);
            temp /= 10;
        }
        for (int j = rev_pos - 1; j >= 0; j--) {
            count_str[pos++] = rev[j];
        }
    }
    count_str[pos] = '\0';
    terminal_writestring(count_str);
    terminal_writestring(" entries in ");
    terminal_writestring(dir_name);
    terminal_writestring("\n");
    
    for (int i = 0; i < count; i++) {
        // Skip directories and special entries
        if (entries[i].attributes & 0x10) {  // Directory
            continue;
        }
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            continue;  // Free or deleted
        }
        
        // Convert 8.3 name to regular filename
        char filename[64];
        int j = 0;
        for (int k = 0; k < 8 && entries[i].name[k] != ' '; k++) {
            filename[j++] = entries[i].name[k];
        }
        if (entries[i].name[8] != ' ') {
            filename[j++] = '.';
            for (int k = 8; k < 11 && entries[i].name[k] != ' '; k++) {
                filename[j++] = entries[i].name[k];
            }
        }
        filename[j] = '\0';
        
        terminal_writestring("Loading file: ");
        terminal_writestring(filename);
        terminal_writestring(" (size: ");
        // Convert file_size to string
        char size_str[16];
        uint32_t file_size = entries[i].file_size;
        temp = file_size;
        pos = 0;
        if (temp == 0) {
            size_str[pos++] = '0';
        } else {
            char rev[16];
            int rev_pos = 0;
            while (temp > 0) {
                rev[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int j = rev_pos - 1; j >= 0; j--) {
                size_str[pos++] = rev[j];
            }
        }
        size_str[pos] = '\0';
        terminal_writestring(size_str);
        terminal_writestring(")\n");
        
        // Read file data (including empty files)
        // For large files, create entry but don't load data (will read from disk when needed)
        uint8_t* file_data = NULL;
        
        if (file_size > 0) {
            // Only load into memory if it fits in the pool
            if (file_size < FILE_DATA_POOL_SIZE && file_data_used + file_size <= FILE_DATA_POOL_SIZE) {
                // Allocate from file data pool
                file_data = &file_data_pool[file_data_used];
                if (fat32_read_file(fs, &entries[i], file_data, file_size) > 0) {
                    file_data_used += file_size;
                } else {
                    file_data = NULL;  // Read failed
                    terminal_writestring_color("Error: Failed to read file data\n", 0x0C);
                }
            } else {
                // File too large - create entry without data (will read from disk when needed)
                terminal_writestring(" (too large for memory, will read from disk)\n");
                file_data = NULL;  // Mark as disk-only file
            }
        } else {
            // Empty file - just create it with NULL data
            file_data = NULL;
        }
        
        // Create file in in-memory filesystem (even if data not loaded)
        fs_node_t* home = fs_find_child(fs_root, "home");
        if (home != NULL) {
            // Check if file already exists (skip if it does)
            if (fs_find_child(home, filename) == NULL) {
                if (fs_create_file(home, filename, file_data, file_size) == 0) {
                    if (file_data != NULL) {
                        terminal_writestring_color("Successfully loaded\n", 0x0A);
                    } else if (file_size > 0) {
                        terminal_writestring_color("File entry created (disk-only)\n", 0x0E);
                    } else {
                        terminal_writestring_color("Successfully loaded\n", 0x0A);
                    }
                } else {
                    terminal_writestring_color("Error: Failed to create file in memory\n", 0x0C);
                }
            } else {
                terminal_writestring("File already exists in memory, skipping\n");
            }
        }
    }
}

int fs_load_from_disk(void) {
    extern fat32_fs_t* fat32_get_fs(void);
    
    fat32_fs_t* fs = fat32_get_fs();
    
    if (fs == NULL || !fs->mounted) {
        return -1;  // No FAT32 filesystem mounted
    }
    
    // Load files from root directory and home subdirectory
    extern void terminal_writestring(const char*);
    extern void terminal_writestring_color(const char*, uint8_t);
    extern int fat32_find_file(fat32_fs_t* fs, uint32_t dir_cluster, const char* filename, fat32_dir_entry_t* entry);
    
    terminal_writestring("Loading files from disk...\n");
    
    // First, find the home directory (try both "home" and "HOME")
    fat32_dir_entry_t home_entry;
    uint32_t home_cluster = 0;
    if (fat32_find_file(fs, fs->root_dir_cluster, "HOME", &home_entry) == 0) {
        if (home_entry.attributes & 0x10) {  // Is a directory
            home_cluster = home_entry.cluster_low | (home_entry.cluster_high << 16);
            terminal_writestring("Found HOME directory on disk\n");
        }
    } else if (fat32_find_file(fs, fs->root_dir_cluster, "home", &home_entry) == 0) {
        if (home_entry.attributes & 0x10) {  // Is a directory
            home_cluster = home_entry.cluster_low | (home_entry.cluster_high << 16);
            terminal_writestring("Found home directory on disk\n");
        }
    }
    
    // Actually call the function to load files from home directory
    if (home_cluster != 0 && home_cluster < 0x0FFFFFF8) {
        load_files_from_dir(fs, home_cluster, "home");
    } else {
        terminal_writestring("No home directory found on disk\n");
    }
    
    return 0;
}

