#include "brainfuck.h"
#include "filesystem.h"
#include "keyboard.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* str);
void terminal_writestring_color(const char* data, uint8_t color);
void terminal_putchar(char c);
int keyboard_getchar(void);
void* malloc(uint32_t size);
void free(void* ptr);
void malloc_reset(void);

// Color constants (VGA colors)
#define COLOR_RED 0x0C  // Light red on black

// Brainfuck interpreter state
#define BF_TAPE_SIZE 30000  // Standard Brainfuck tape size
static uint8_t* bf_tape = NULL;
static uint32_t bf_tape_allocated = 0;

// Find matching bracket (for [ and ])
static uint32_t bf_find_matching_bracket(const char* code, uint32_t pos, int forward) {
    int depth = 1;
    uint32_t i = pos;
    
    if (forward) {
        // Find matching ] for [
        i++;
        while (code[i] != '\0' && depth > 0) {
            if (code[i] == '[') depth++;
            else if (code[i] == ']') depth--;
            i++;
        }
        if (depth == 0) return i - 1;
    } else {
        // Find matching [ for ]
        i--;
        while (i > 0 && depth > 0) {
            if (code[i] == ']') depth++;
            else if (code[i] == '[') depth--;
            i--;
        }
        if (depth == 0) return i;
    }
    
    return 0; // Not found
}

// Execute Brainfuck program
int brainfuck_execute(const char* source) {
    if (source == NULL) {
        terminal_writestring_color("Error: NULL source code\n", COLOR_RED);
        return -1;
    }
    
    // Allocate tape if not already allocated
    if (bf_tape == NULL) {
        bf_tape = (uint8_t*)malloc(BF_TAPE_SIZE);
        if (bf_tape == NULL) {
            terminal_writestring_color("Error: Failed to allocate Brainfuck tape\n", COLOR_RED);
            return -1;
        }
        bf_tape_allocated = 1;
    }
    
    // Initialize tape to zeros
    for (uint32_t i = 0; i < BF_TAPE_SIZE; i++) {
        bf_tape[i] = 0;
    }
    
    uint32_t tape_ptr = 0;  // Pointer into tape
    uint32_t code_ptr = 0;   // Pointer into source code
    
    // Execution loop
    while (source[code_ptr] != '\0') {
        char cmd = source[code_ptr];
        
        switch (cmd) {
            case '>':
                // Increment pointer
                if (tape_ptr < BF_TAPE_SIZE - 1) {
                    tape_ptr++;
                } else {
                    terminal_writestring_color("Error: Tape pointer overflow\n", COLOR_RED);
                    return -1;
                }
                break;
                
            case '<':
                // Decrement pointer
                if (tape_ptr > 0) {
                    tape_ptr--;
                } else {
                    terminal_writestring_color("Error: Tape pointer underflow\n", COLOR_RED);
                    return -1;
                }
                break;
                
            case '+':
                // Increment value at pointer
                bf_tape[tape_ptr]++;
                break;
                
            case '-':
                // Decrement value at pointer
                bf_tape[tape_ptr]--;
                break;
                
            case '.':
                // Output character
                terminal_putchar((char)bf_tape[tape_ptr]);
                break;
                
            case ',':
                // Input character
                {
                    // Poll keyboard and get input
                    extern void keyboard_handler(void);
                    int c = -1;
                    int attempts = 0;
                    while (c == -1 && attempts < 10000) {
                        keyboard_handler();
                        keyboard_handler();
                        c = keyboard_getchar();
                        if (c == -1) {
                            // Small delay
                            for (volatile int i = 0; i < 100; i++);
                            attempts++;
                        }
                    }
                    
                    if (c == -1) {
                        // No input available, use 0
                        bf_tape[tape_ptr] = 0;
                    } else {
                        bf_tape[tape_ptr] = (uint8_t)c;
                    }
                }
                break;
                
            case '[':
                // Start loop - if value at pointer is 0, jump to matching ]
                if (bf_tape[tape_ptr] == 0) {
                    uint32_t match = bf_find_matching_bracket(source, code_ptr, 1);
                    if (match == 0) {
                        terminal_writestring_color("Error: Unmatched '['\n", COLOR_RED);
                        return -1;
                    }
                    code_ptr = match;
                }
                break;
                
            case ']':
                // End loop - if value at pointer is not 0, jump to matching [
                if (bf_tape[tape_ptr] != 0) {
                    uint32_t match = bf_find_matching_bracket(source, code_ptr, 0);
                    if (match == 0) {
                        terminal_writestring_color("Error: Unmatched ']'\n", COLOR_RED);
                        return -1;
                    }
                    code_ptr = match;
                }
                break;
                
            // Ignore all other characters (comments, whitespace, etc.)
            default:
                break;
        }
        
        code_ptr++;
    }
    
    return 0;
}

// Load and run from file
int brainfuck_load_and_run(const char* path) {
    fs_node_t* file = fs_resolve_path(path);
    if (!file || file->type != FS_FILE) {
        terminal_writestring_color("Brainfuck file not found: ", COLOR_RED);
        terminal_writestring(path);
        terminal_writestring_color("\n", COLOR_RED);
        return -1;
    }
    
    uint32_t size = fs_get_file_size(file);
    if (size == 0) {
        terminal_writestring_color("Error: Empty Brainfuck file\n", COLOR_RED);
        return -1;
    }
    
    char* source = (char*)malloc(size + 1);
    if (!source) {
        terminal_writestring_color("Error: Failed to allocate memory for source\n", COLOR_RED);
        return -1;
    }
    
    fs_read_file(file, (uint8_t*)source, size);
    source[size] = '\0';
    
    int result = brainfuck_execute(source);
    free(source);
    return result;
}

// Cleanup
void brainfuck_cleanup(void) {
    // Reset malloc pool to free all allocated memory (including tape)
    malloc_reset();
    bf_tape = NULL;
    bf_tape_allocated = 0;
}

