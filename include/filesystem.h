#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "types.h"

#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 64
#define MAX_DIR_ENTRIES 64

// File types
typedef enum {
    FS_FILE,
    FS_DIRECTORY
} fs_node_type_t;

// File system node
typedef struct fs_node {
    char name[MAX_FILENAME_LENGTH];
    fs_node_type_t type;
    struct fs_node* parent;
    struct fs_node* children[MAX_DIR_ENTRIES];
    uint32_t child_count;
    // File data (for files only)
    uint8_t* data;
    uint32_t data_size;
} fs_node_t;

// File system root
extern fs_node_t* fs_root;
extern fs_node_t* fs_current_dir;

void fs_init(void);
fs_node_t* fs_create_node(const char* name, fs_node_type_t type, fs_node_t* parent);
fs_node_t* fs_find_child(fs_node_t* dir, const char* name);
fs_node_t* fs_resolve_path(const char* path);
int fs_mkdir(fs_node_t* parent, const char* name);
void fs_list_directory(fs_node_t* dir);
fs_node_t* fs_find_program(const char* name); // Find program in /sys/components

// File operations
int fs_create_file(fs_node_t* parent, const char* name, const uint8_t* data, uint32_t size);
int fs_read_file(fs_node_t* file, uint8_t* buffer, uint32_t size);
int fs_read_file_at(fs_node_t* file, uint32_t offset, uint8_t* buffer, uint32_t size);  // Read with offset
int fs_write_file(fs_node_t* file, const uint8_t* data, uint32_t size);  // Write to file
uint32_t fs_get_file_size(fs_node_t* file);

// Disk operations
int fs_save_to_disk(void);  // Save in-memory filesystem to disk
int fs_load_from_disk(void);  // Load filesystem from disk

// Initialize filesystem from sys/ directory (generated at build time)
void sysfs_initialize(void);

#endif

