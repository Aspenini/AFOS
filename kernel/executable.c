#include "executable.h"
#include "filesystem.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);

// Color constants (VGA colors)
#define COLOR_RED 0x0C  // Light red on black

// Simple memory allocator for executables (very basic)
#define EXEC_MEMORY_SIZE (1024 * 1024)  // 1MB for executables
static uint8_t exec_memory[EXEC_MEMORY_SIZE];

// Load and execute an AFOS binary
int exec_load_and_run(const char* path, int argc, char** argv) {
    // Resolve path to file node
    fs_node_t* file = fs_resolve_path(path);
    if (file == NULL || file->type != FS_FILE) {
        terminal_writestring_color("File not found: ", COLOR_RED);
        terminal_writestring(path);
        terminal_writestring_color("\n", COLOR_RED);
        return -1;
    }
    
    // Get file size
    uint32_t file_size = fs_get_file_size(file);
    if (file_size < sizeof(struct afos_exec_header)) {
        terminal_writestring_color("Invalid executable: file too small\n", COLOR_RED);
        return -1;
    }
    
    // Read file data
    uint8_t* file_data = exec_memory;
    if (file_size > EXEC_MEMORY_SIZE) {
        terminal_writestring_color("Executable too large\n", COLOR_RED);
        return -1;
    }
    
    int read_size = fs_read_file(file, file_data, file_size);
    if (read_size < 0) {
        terminal_writestring_color("Failed to read file\n", COLOR_RED);
        return -1;
    }
    
    // Validate executable
    if (!exec_is_valid(file_data, file_size)) {
        terminal_writestring_color("Invalid AFOS executable format\n", COLOR_RED);
        return -1;
    }
    
    struct afos_exec_header* header = (struct afos_exec_header*)file_data;
    
    // Check that we have enough data
    if (file_size < sizeof(struct afos_exec_header) + header->code_size) {
        terminal_writestring_color("Invalid executable: incomplete code section\n", COLOR_RED);
        return -1;
    }
    
    // Get code section
    uint8_t* code = file_data + sizeof(struct afos_exec_header);
    
    // Check entry point is valid
    if (header->entry_point >= header->code_size) {
        terminal_writestring_color("Invalid executable: entry point out of bounds\n", COLOR_RED);
        return -1;
    }
    
    // Calculate entry point address
    exec_entry_t entry = (exec_entry_t)(code + header->entry_point);
    
    // Execute the program
    // Note: In a real OS, you'd set up a proper process context
    // For now, we'll just call it directly
    int result = entry(argc, argv);
    
    return result;
}

// Check if data is a valid AFOS executable
int exec_is_valid(const uint8_t* data, uint32_t size) {
    if (data == NULL || size < sizeof(struct afos_exec_header)) {
        return 0;
    }
    
    struct afos_exec_header* header = (struct afos_exec_header*)data;
    
    // Check magic number
    if (header->magic != AFOS_EXEC_MAGIC) {
        return 0;
    }
    
    // Check version
    if (header->version != AFOS_EXEC_VERSION) {
        return 0;
    }
    
    // Check sizes are reasonable
    if (header->code_size > EXEC_MEMORY_SIZE || 
        header->code_size == 0 ||
        header->entry_point >= header->code_size) {
        return 0;
    }
    
    return 1;
}

