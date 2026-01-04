#include "filesystem.h"
#include "types.h"

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
    
    // Try with common extensions (including .bas for BASIC programs)
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
    
    // Check if we have enough space
    if (file_data_used + size > FILE_DATA_POOL_SIZE) {
        return -1;
    }
    
    // Create file node
    fs_node_t* file = fs_create_node(name, FS_FILE, parent);
    if (file == NULL) {
        return -1;
    }
    
    // Allocate and copy data
    file->data = &file_data_pool[file_data_used];
    file->data_size = size;
    file_data_used += size;
    
    // Copy data
    for (uint32_t i = 0; i < size; i++) {
        file->data[i] = data[i];
    }
    
    return 0;
}

// Read file data
int fs_read_file(fs_node_t* file, uint8_t* buffer, uint32_t size) {
    if (file == NULL || file->type != FS_FILE || buffer == NULL) {
        return -1;
    }
    
    uint32_t to_copy = size < file->data_size ? size : file->data_size;
    for (uint32_t i = 0; i < to_copy; i++) {
        buffer[i] = file->data[i];
    }
    
    return to_copy;
}

// Get file size
uint32_t fs_get_file_size(fs_node_t* file) {
    if (file == NULL || file->type != FS_FILE) {
        return 0;
    }
    return file->data_size;
}

