#include "shell.h"
#include "filesystem.h"
#include "keyboard.h"
#include "executable.h"
#include "basic.h"
#include "brainfuck.h"
#include "graphics.h"
#include "icmp.h"
#include "ip.h"
#include "ethernet.h"
#include "audio.h"
#include "wav.h"
#include "ac97.h"
#include "types.h"

// Forward declarations - these are in kernel.c
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);
void terminal_putchar(char c);
void terminal_clear(void);

// Color constants (VGA colors)
#define COLOR_RED     0x0C  // Light red on black
#define COLOR_GREEN   0x0A  // Light green on black
#define COLOR_YELLOW  0x0E  // Yellow on black
#define COLOR_DEFAULT 0x0F  // White on black

static char shell_buffer[SHELL_BUFFER_SIZE];
static uint32_t shell_buffer_pos = 0;

// Get current directory path
static void shell_get_path(char* path, size_t max_len) {
    if (fs_current_dir == NULL) {
        path[0] = '\0';
        return;
    }
    
    // Build path by traversing up to root
    char temp_path[MAX_PATH_LENGTH];
    uint32_t pos = MAX_PATH_LENGTH - 1;
    temp_path[pos] = '\0';
    
    fs_node_t* node = fs_current_dir;
    while (node != NULL && node != fs_root) {
        // Find name length
        uint32_t name_len = 0;
        while (node->name[name_len] != '\0') {
            name_len++;
        }
        
        // Add name to path
        pos -= name_len;
        if (pos < 0) break;
        
        for (uint32_t i = 0; i < name_len; i++) {
            temp_path[pos + i] = node->name[i];
        }
        
        // Add slash
        if (pos > 0) {
            pos--;
            temp_path[pos] = '/';
        }
        
        node = node->parent;
    }
    
    // Add root slash
    if (pos > 0) {
        pos--;
        temp_path[pos] = '/';
    }
    
    // Copy to output
    uint32_t i = 0;
    while (temp_path[pos + i] != '\0' && i < max_len - 1) {
        path[i] = temp_path[pos + i];
        i++;
    }
    path[i] = '\0';
    
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
}

// Print shell prompt
void shell_print_prompt(void) {
    char path[MAX_PATH_LENGTH];
    shell_get_path(path, MAX_PATH_LENGTH);
    terminal_writestring("AFOS:");
    terminal_writestring(path);
    terminal_writestring("$ ");
}

// Split command into arguments
static uint32_t shell_parse_args(char* input, char* args[], uint32_t max_args) {
    uint32_t arg_count = 0;
    uint32_t i = 0;
    int in_arg = 0;
    
    while (input[i] != '\0' && arg_count < max_args) {
        if (input[i] == ' ' || input[i] == '\t') {
            if (in_arg) {
                input[i] = '\0';
                in_arg = 0;
            }
        } else {
            if (!in_arg) {
                args[arg_count++] = &input[i];
                in_arg = 1;
            }
        }
        i++;
    }
    
    if (in_arg && arg_count < max_args) {
        args[arg_count] = NULL;
    } else {
        args[arg_count] = NULL;
    }
    
    return arg_count;
}

// Execute cd command
static void shell_cd(char* args[], uint32_t arg_count) {
    if (arg_count < 2) {
        terminal_writestring_color("cd: missing argument\n", COLOR_RED);
        return;
    }
    
    fs_node_t* target = fs_resolve_path(args[1]);
    if (target == NULL) {
        terminal_writestring_color("cd: directory not found: ", COLOR_RED);
        terminal_writestring(args[1]);
        terminal_writestring_color("\n", COLOR_RED);
        return;
    }
    
    if (target->type != FS_DIRECTORY) {
        terminal_writestring_color("cd: not a directory: ", COLOR_RED);
        terminal_writestring(args[1]);
        terminal_writestring_color("\n", COLOR_RED);
        return;
    }
    
    fs_current_dir = target;
}

// Execute clear command
static void shell_clear(void) {
    terminal_clear();
}

// Execute help command
static void shell_help(void) {
    terminal_writestring("AFOS Shell - Available Commands\n");
    terminal_writestring("================================\n\n");
    
    terminal_writestring("Built-in Commands:\n");
    terminal_writestring("  cd <dir>        - Change directory\n");
    terminal_writestring("  ls [dir]        - List directory contents\n");
    terminal_writestring("  dir [dir]       - List directory contents (alias for ls)\n");
    terminal_writestring("  clear           - Clear the screen\n");
    terminal_writestring("  run <executable> - Run an AFOS executable\n");
    terminal_writestring("  graphics-test   - Run graphics demo\n");
    terminal_writestring("  audio-test      - Test audio output (plays a tone)\n");
    terminal_writestring("  play <file.wav> - Play a WAV audio file\n");
    terminal_writestring("  save            - Save files to disk (FAT32)\n");
    terminal_writestring("  create <file>   - Create a new empty file\n");
    terminal_writestring("  help            - Show this help message\n");
    terminal_writestring("\n");
    terminal_writestring("BASIC Programs:\n");
    terminal_writestring("  Run .bas files directly: file.bas\n");
    terminal_writestring("  Or: run file.bas\n");
    terminal_writestring("\n");
    
    // List programs in /sys/components
    fs_node_t* sys = fs_find_child(fs_root, "sys");
    if (sys != NULL) {
        fs_node_t* components = fs_find_child(sys, "components");
        if (components != NULL && components->child_count > 0) {
            terminal_writestring("Available Programs in /sys/components:\n");
            for (uint32_t i = 0; i < components->child_count; i++) {
                if (components->children[i] != NULL) {
                    terminal_writestring("  ");
                    terminal_writestring(components->children[i]->name);
                    // Strip extension for display
                    uint32_t len = 0;
                    while (components->children[i]->name[len] != '\0') len++;
                    if (len > 5) {
                        // Check for .afos extension
                        if (components->children[i]->name[len-5] == '.' &&
                            components->children[i]->name[len-4] == 'a' &&
                            components->children[i]->name[len-3] == 'f' &&
                            components->children[i]->name[len-2] == 'o' &&
                            components->children[i]->name[len-1] == 's') {
                            terminal_writestring(" (run as: ");
                            for (uint32_t k = 0; k < len - 5; k++) {
                                terminal_putchar(components->children[i]->name[k]);
                            }
                            terminal_writestring(")");
                        }
                    }
                    terminal_writestring("\n");
                }
            }
        } else {
            terminal_writestring("No programs found in /sys/components\n");
        }
    }
    
    terminal_writestring("\n");
    terminal_writestring("Note: Programs in /sys/components can be run by typing their name\n");
    terminal_writestring("      (without the .afos extension)\n");
}

// Execute ls/dir command
static void shell_ls(char* args[], uint32_t arg_count) {
    fs_node_t* dir = fs_current_dir;
    
    // If argument provided, resolve path
    if (arg_count >= 2) {
        dir = fs_resolve_path(args[1]);
        if (dir == NULL) {
            terminal_writestring_color("ls: directory not found: ", COLOR_RED);
            terminal_writestring(args[1]);
            terminal_writestring_color("\n", COLOR_RED);
            return;
        }
        
        if (dir->type != FS_DIRECTORY) {
            terminal_writestring_color("ls: not a directory: ", COLOR_RED);
            terminal_writestring(args[1]);
            terminal_writestring_color("\n", COLOR_RED);
            return;
        }
    }
    
    // List directory contents
    if (dir->child_count == 0) {
        terminal_writestring("(empty)\n");
        return;
    }
    
    for (uint32_t i = 0; i < dir->child_count; i++) {
        if (dir->children[i] != NULL) {
            terminal_writestring(dir->children[i]->name);
            if (dir->children[i]->type == FS_DIRECTORY) {
                terminal_writestring("/");
            }
            terminal_writestring("  ");
        }
    }
    terminal_writestring("\n");
}

// Process command
void shell_process_command(const char* input) {
    if (input == NULL || input[0] == '\0') {
        return;
    }
    
    // Copy input to work buffer
    char work_buffer[SHELL_BUFFER_SIZE];
    uint32_t i = 0;
    while (input[i] != '\0' && i < SHELL_BUFFER_SIZE - 1) {
        work_buffer[i] = input[i];
        i++;
    }
    work_buffer[i] = '\0';
    
    // Parse arguments
    char* args[SHELL_MAX_ARGS];
    uint32_t arg_count = shell_parse_args(work_buffer, args, SHELL_MAX_ARGS);
    
    if (arg_count == 0) {
        return;
    }
    
    // Execute command
    uint32_t j = 0;
    int match = 1;
    const char* cmd = "cd";
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        shell_cd(args, arg_count);
        return;
    }
    
    match = 1;
    cmd = "ls";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        shell_ls(args, arg_count);
        return;
    }
    
    match = 1;
    cmd = "dir";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        shell_ls(args, arg_count);
        return;
    }
    
    match = 1;
    cmd = "clear";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        shell_clear();
        return;
    }
    
    match = 1;
    cmd = "run";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        if (arg_count < 2) {
            terminal_writestring_color("run: missing argument\n", COLOR_RED);
            terminal_writestring_color("Usage: run <path/to/file> (must specify full or relative path)\n", COLOR_YELLOW);
            return;
        }
        // Check if it's a .bas file
        uint32_t run_arg_len = 0;
        while (args[1][run_arg_len] != '\0') run_arg_len++;
        if (run_arg_len >= 4) {
            if (args[1][run_arg_len-4] == '.' &&
                args[1][run_arg_len-3] == 'b' &&
                args[1][run_arg_len-2] == 'a' &&
                args[1][run_arg_len-1] == 's') {
                // Run with BASIC interpreter
                basic_load_and_run(args[1]);
                basic_cleanup();
                return;
            }
            // Check if it's a .bf file
            if (run_arg_len >= 3) {
                if (args[1][run_arg_len-3] == '.' &&
                    args[1][run_arg_len-2] == 'b' &&
                    args[1][run_arg_len-1] == 'f') {
                    // Run with Brainfuck interpreter
                    brainfuck_load_and_run(args[1]);
                    brainfuck_cleanup();
                    return;
                }
            }
        }
        // Execute the binary (run command only accepts paths, not system path names)
        exec_load_and_run(args[1], arg_count - 1, &args[1]);
        return;
    }
    
    match = 1;
    cmd = "graphics-test";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        // Run graphics test (VGA mode 13h: 320x200x8)
        terminal_writestring("Initializing VGA graphics (mode 13h: 320x200x8)...\n");
        if (gfx_init(320, 200, 8) == 0) {
            // Graphics mode is now active - screen may be blank/black
            // Run demo immediately (no terminal output visible in graphics mode)
            gfx_demo();
            
            // Wait 5 seconds before closing graphics
            // Use a busy-wait loop (approximate timing, depends on CPU speed)
            for (volatile uint32_t outer = 0; outer < 5000; outer++) {
                for (volatile uint32_t inner = 0; inner < 10000; inner++) {
                    // Busy wait
                }
            }
            
            // Switch back to text mode before printing
            gfx_shutdown();
            
            // Now we can print to terminal
            terminal_writestring("Returned to text mode.\n");
        } else {
            terminal_writestring_color("Error: Failed to initialize graphics\n", COLOR_RED);
        }
        return;
    }
    
    match = 1;
    cmd = "audio-test";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        // Test audio output
        terminal_writestring("Testing audio output...\n");
        terminal_writestring("Playing 440Hz tone for 2 seconds...\n");
        
        extern int audio_generate_tone(uint32_t frequency_hz, uint32_t duration_ms, uint32_t sample_rate);
        if (audio_generate_tone(440, 2000, 22050) == 0) {
            terminal_writestring_color("Audio test completed successfully!\n", COLOR_GREEN);
        } else {
            terminal_writestring_color("Audio test failed. Make sure AC97 is initialized.\n", COLOR_RED);
        }
        return;
    }
    
    // Play WAV file command
    match = 1;
    cmd = "play";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        // Play WAV file
        if (arg_count < 2) {
            terminal_writestring_color("Usage: play <filename.wav>\n", COLOR_YELLOW);
            return;
        }
        
        // Resolve file path
        extern fs_node_t* fs_resolve_path(const char* path);
        extern uint32_t fs_get_file_size(fs_node_t* file);
        extern int fs_read_file(fs_node_t* file, uint8_t* buffer, uint32_t size);
        extern void* malloc(uint32_t size);
        extern void free(void* ptr);
        
        fs_node_t* file = fs_resolve_path(args[1]);
        if (file == NULL || file->type != FS_FILE) {
            terminal_writestring_color("Error: File not found\n", COLOR_RED);
            return;
        }
        
        uint32_t file_size = fs_get_file_size(file);
        if (file_size == 0) {
            terminal_writestring_color("Error: File is empty\n", COLOR_RED);
            return;
        }
        
        // Read header first (first 4KB should be enough for WAV header)
        uint32_t header_size = 4096;
        if (header_size > file_size) {
            header_size = file_size;
        }
        
        uint8_t* header_data = malloc(header_size);
        if (header_data == NULL) {
            terminal_writestring_color("Error: Out of memory\n", COLOR_RED);
            return;
        }
        
        if (fs_read_file(file, header_data, header_size) != header_size) {
            terminal_writestring_color("Error: Failed to read file header\n", COLOR_RED);
            free(header_data);
            return;
        }
        
        // Parse WAV header
        wav_file_t wav;
        if (wav_parse(header_data, header_size, &wav) != 0) {
            terminal_writestring_color("Error: Invalid WAV file\n", COLOR_RED);
            free(header_data);
            return;
        }
        
        // Display WAV info
        terminal_writestring("WAV file info:\n");
        terminal_writestring("  Sample rate: ");
        // Print sample rate
        char rate_str[16];
        int temp = wav.sample_rate;
        int pos = 0;
        if (temp == 0) {
            rate_str[pos++] = '0';
        } else {
            char rev[16];
            int rev_pos = 0;
            while (temp > 0) {
                rev[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int k = rev_pos - 1; k >= 0; k--) {
                rate_str[pos++] = rev[k];
            }
        }
        rate_str[pos] = '\0';
        terminal_writestring(rate_str);
        terminal_writestring(" Hz\n");
        
        terminal_writestring("  Channels: ");
        terminal_putchar('0' + wav.num_channels);
        terminal_writestring("\n");
        
        terminal_writestring("  Bit depth: ");
        // Print bit depth
        char bits_str[8];
        temp = wav.bits_per_sample;
        pos = 0;
        if (temp == 0) {
            bits_str[pos++] = '0';
        } else {
            char rev[8];
            int rev_pos = 0;
            while (temp > 0) {
                rev[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int k = rev_pos - 1; k >= 0; k--) {
                bits_str[pos++] = rev[k];
            }
        }
        bits_str[pos] = '\0';
        terminal_writestring(bits_str);
        terminal_writestring(" bits\n");
        
        // Calculate where PCM data starts in the file
        uint32_t pcm_data_offset = (uint32_t)(wav.pcm_data - header_data);
        if (pcm_data_offset >= header_size) {
            // PCM data is not in header buffer, need to read more to find it
            // Re-read a larger header to find PCM data start
            free(header_data);
            uint32_t larger_header = 8192;  // 8KB should be enough
            if (larger_header > file_size) {
                larger_header = file_size;
            }
            header_data = malloc(larger_header);
            if (header_data == NULL) {
                terminal_writestring_color("Error: Out of memory\n", COLOR_RED);
                return;
            }
            if (fs_read_file(file, header_data, larger_header) != larger_header) {
                terminal_writestring_color("Error: Failed to read file header\n", COLOR_RED);
                free(header_data);
                return;
            }
            // Re-parse
            if (wav_parse(header_data, larger_header, &wav) != 0) {
                terminal_writestring_color("Error: Invalid WAV file\n", COLOR_RED);
                free(header_data);
                return;
            }
            pcm_data_offset = (uint32_t)(wav.pcm_data - header_data);
            if (pcm_data_offset >= larger_header) {
                terminal_writestring_color("Error: PCM data offset too large\n", COLOR_RED);
                free(header_data);
                return;
            }
        }
        
        // Free header data - we'll stream PCM data directly from disk
        free(header_data);
        
        // Stream PCM data in chunks directly from disk
        // Use 64KB chunks for reading and conversion (enough for ~1 second at 44.1kHz)
        const uint32_t CHUNK_SIZE = 65536;
        uint8_t* pcm_chunk = malloc(CHUNK_SIZE);
        uint8_t* converted_chunk = malloc(CHUNK_SIZE);
        if (pcm_chunk == NULL || converted_chunk == NULL) {
            terminal_writestring_color("Error: Out of memory for streaming\n", COLOR_RED);
            if (pcm_chunk) free(pcm_chunk);
            if (converted_chunk) free(converted_chunk);
            return;
        }
        
        terminal_writestring("Streaming audio...\n");
        terminal_writestring("Total PCM size: ");
        // Print PCM size
        char pcm_size_str[16];
        int pcm_temp = wav.pcm_size;
        int pcm_pos = 0;
        if (pcm_temp == 0) {
            pcm_size_str[pcm_pos++] = '0';
        } else {
            char rev[16];
            int rev_pos = 0;
            while (pcm_temp > 0) {
                rev[rev_pos++] = '0' + (pcm_temp % 10);
                pcm_temp /= 10;
            }
            for (int k = rev_pos - 1; k >= 0; k--) {
                pcm_size_str[pcm_pos++] = rev[k];
            }
        }
        pcm_size_str[pcm_pos] = '\0';
        terminal_writestring(pcm_size_str);
        terminal_writestring(" bytes\n");
        
        // Stream and play PCM data in chunks
        extern int fs_read_file_at(fs_node_t* file, uint32_t offset, uint8_t* buffer, uint32_t size);
        uint32_t pcm_offset = 0;
        uint32_t chunk_num = 0;
        
        terminal_writestring("Starting playback loop...\n");
        while (pcm_offset < wav.pcm_size) {
            terminal_writestring("Loop iteration, pcm_offset=");
            // Print pcm_offset
            char loop_off_str[16];
            int loop_off_temp = pcm_offset;
            int loop_off_pos = 0;
            if (loop_off_temp == 0) {
                loop_off_str[loop_off_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (loop_off_temp > 0) {
                    rev[rev_pos++] = '0' + (loop_off_temp % 10);
                    loop_off_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    loop_off_str[loop_off_pos++] = rev[k];
                }
            }
            loop_off_str[loop_off_pos] = '\0';
            terminal_writestring(loop_off_str);
            terminal_writestring(", pcm_size=");
            // Print wav.pcm_size
            char loop_size_str[16];
            int loop_size_temp = wav.pcm_size;
            int loop_size_pos = 0;
            if (loop_size_temp == 0) {
                loop_size_str[loop_size_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (loop_size_temp > 0) {
                    rev[rev_pos++] = '0' + (loop_size_temp % 10);
                    loop_size_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    loop_size_str[loop_size_pos++] = rev[k];
                }
            }
            loop_size_str[loop_size_pos] = '\0';
            terminal_writestring(loop_size_str);
            terminal_writestring("\n");
            
            chunk_num++;
            uint32_t chunk_size = CHUNK_SIZE;
            if (chunk_size > (wav.pcm_size - pcm_offset)) {
                chunk_size = wav.pcm_size - pcm_offset;
            }
            
            // Debug: Show what we're about to read
            terminal_writestring("Reading chunk ");
            // Print chunk number
            char pre_chunk_str[8];
            int pre_chunk_temp = chunk_num;
            int pre_chunk_pos = 0;
            if (pre_chunk_temp == 0) {
                pre_chunk_str[pre_chunk_pos++] = '0';
            } else {
                char rev[8];
                int rev_pos = 0;
                while (pre_chunk_temp > 0) {
                    rev[rev_pos++] = '0' + (pre_chunk_temp % 10);
                    pre_chunk_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    pre_chunk_str[pre_chunk_pos++] = rev[k];
                }
            }
            pre_chunk_str[pre_chunk_pos] = '\0';
            terminal_writestring(pre_chunk_str);
            terminal_writestring(" at offset ");
            // Print offset
            char off_str[16];
            int off_temp = pcm_data_offset + pcm_offset;
            int off_pos = 0;
            if (off_temp == 0) {
                off_str[off_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (off_temp > 0) {
                    rev[rev_pos++] = '0' + (off_temp % 10);
                    off_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    off_str[off_pos++] = rev[k];
                }
            }
            off_str[off_pos] = '\0';
            terminal_writestring(off_str);
            terminal_writestring(", size ");
            // Print chunk_size
            char size_str[16];
            int size_temp = chunk_size;
            int size_pos = 0;
            if (size_temp == 0) {
                size_str[size_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (size_temp > 0) {
                    rev[rev_pos++] = '0' + (size_temp % 10);
                    size_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    size_str[size_pos++] = rev[k];
                }
            }
            size_str[size_pos] = '\0';
            terminal_writestring(size_str);
            terminal_writestring("...\n");
            
            // Read PCM chunk directly from disk at offset
            int bytes_read = fs_read_file_at(file, pcm_data_offset + pcm_offset, pcm_chunk, chunk_size);
            if (bytes_read <= 0) {
                terminal_writestring_color("Error: Failed to read PCM data from disk (returned: ", COLOR_RED);
                // Print bytes_read
                char err_str[16];
                int err_temp = bytes_read;
                int err_pos = 0;
                if (err_temp < 0) {
                    err_str[err_pos++] = '-';
                    err_temp = -err_temp;
                }
                if (err_temp == 0) {
                    err_str[err_pos++] = '0';
                } else {
                    char rev[16];
                    int rev_pos = 0;
                    while (err_temp > 0) {
                        rev[rev_pos++] = '0' + (err_temp % 10);
                        err_temp /= 10;
                    }
                    for (int k = rev_pos - 1; k >= 0; k--) {
                        err_str[err_pos++] = rev[k];
                    }
                }
                err_str[err_pos] = '\0';
                terminal_writestring(err_str);
                terminal_writestring_color(")\n", COLOR_RED);
                break;
            }
            
            // Create temporary wav structure for this chunk
            wav_file_t chunk_wav = wav;
            chunk_wav.pcm_data = pcm_chunk;
            chunk_wav.pcm_size = bytes_read;
            
            // Convert chunk to 8-bit mono
            uint32_t samples_in_chunk = wav_convert_to_8bit_mono(&chunk_wav, converted_chunk, CHUNK_SIZE);
            if (samples_in_chunk == 0) {
                terminal_writestring_color("Error: Failed to convert audio chunk\n", COLOR_RED);
                break;
            }
            
            // Play this chunk
            terminal_writestring("Playing chunk ");
            // Print chunk number
            char chunk_str[8];
            int chunk_temp = chunk_num;
            int chunk_pos = 0;
            if (chunk_temp == 0) {
                chunk_str[chunk_pos++] = '0';
            } else {
                char rev[8];
                int rev_pos = 0;
                while (chunk_temp > 0) {
                    rev[rev_pos++] = '0' + (chunk_temp % 10);
                    chunk_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    chunk_str[chunk_pos++] = rev[k];
                }
            }
            chunk_str[chunk_pos] = '\0';
            terminal_writestring(chunk_str);
            terminal_writestring(" (");
            // Print bytes_read
            char bytes_str[16];
            int bytes_temp = bytes_read;
            int bytes_pos = 0;
            if (bytes_temp == 0) {
                bytes_str[bytes_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (bytes_temp > 0) {
                    rev[rev_pos++] = '0' + (bytes_temp % 10);
                    bytes_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    bytes_str[bytes_pos++] = rev[k];
                }
            }
            bytes_str[bytes_pos] = '\0';
            terminal_writestring(bytes_str);
            terminal_writestring(" bytes, ");
            // Print samples
            char samples_str[16];
            int samples_temp = samples_in_chunk;
            int samples_pos = 0;
            if (samples_temp == 0) {
                samples_str[samples_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (samples_temp > 0) {
                    rev[rev_pos++] = '0' + (samples_temp % 10);
                    samples_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    samples_str[samples_pos++] = rev[k];
                }
            }
            samples_str[samples_pos] = '\0';
            terminal_writestring(samples_str);
            terminal_writestring(" samples)...\n");
            
            terminal_writestring("Calling ac97_play_pcm...\n");
            if (ac97_play_pcm(converted_chunk, samples_in_chunk, wav.sample_rate) != 0) {
                terminal_writestring_color("Error: Playback failed\n", COLOR_RED);
                break;
            }
            terminal_writestring("ac97_play_pcm returned, continuing loop...\n");
            
            pcm_offset += bytes_read;
            
            // Debug: Show progress
            terminal_writestring("Progress: ");
            // Print pcm_offset
            char prog_str[16];
            int prog_temp = pcm_offset;
            int prog_pos = 0;
            if (prog_temp == 0) {
                prog_str[prog_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (prog_temp > 0) {
                    rev[rev_pos++] = '0' + (prog_temp % 10);
                    prog_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    prog_str[prog_pos++] = rev[k];
                }
            }
            prog_str[prog_pos] = '\0';
            terminal_writestring(prog_str);
            terminal_writestring(" / ");
            // Print wav.pcm_size
            char total_str[16];
            int total_temp = wav.pcm_size;
            int total_pos = 0;
            if (total_temp == 0) {
                total_str[total_pos++] = '0';
            } else {
                char rev[16];
                int rev_pos = 0;
                while (total_temp > 0) {
                    rev[rev_pos++] = '0' + (total_temp % 10);
                    total_temp /= 10;
                }
                for (int k = rev_pos - 1; k >= 0; k--) {
                    total_str[total_pos++] = rev[k];
                }
            }
            total_str[total_pos] = '\0';
            terminal_writestring(total_str);
            terminal_writestring(" bytes\n");
        }
        
        terminal_writestring_color("Playback completed!\n", COLOR_GREEN);
        
        // Cleanup
        free(pcm_chunk);
        free(converted_chunk);
        return;
    }
    
    // Save command
    match = 1;
    cmd = "save";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        extern int fs_save_to_disk(void);
        terminal_writestring("Saving files to disk...\n");
        if (fs_save_to_disk() == 0) {
            terminal_writestring_color("Files saved successfully!\n", COLOR_GREEN);
        } else {
            terminal_writestring_color("Error: Failed to save files to disk\n", COLOR_RED);
            terminal_writestring_color("Make sure FAT32 filesystem is mounted\n", COLOR_RED);
        }
        return;
    }
    
    match = 1;
    cmd = "create";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        if (arg_count < 2) {
            terminal_writestring_color("Usage: create <filename>\n", COLOR_YELLOW);
            terminal_writestring_color("Example: create chicken.txt\n", COLOR_YELLOW);
        } else {
            // Create file in current directory
            if (fs_current_dir == NULL) {
                terminal_writestring_color("Error: No current directory\n", COLOR_RED);
            } else {
                // Check if file already exists
                if (fs_find_child(fs_current_dir, args[1]) != NULL) {
                    terminal_writestring_color("Error: File already exists: ", COLOR_RED);
                    terminal_writestring_color(args[1], COLOR_RED);
                    terminal_writestring_color("\n", COLOR_RED);
                } else {
                    // Create empty file (NULL data, size 0)
                    if (fs_create_file(fs_current_dir, args[1], NULL, 0) == 0) {
                        terminal_writestring_color("File created: ", COLOR_GREEN);
                        terminal_writestring(args[1]);
                        terminal_writestring_color("\n", COLOR_GREEN);
                    } else {
                        terminal_writestring_color("Error: Failed to create file\n", COLOR_RED);
                    }
                }
            }
        }
        return;
    }
    
    match = 1;
    cmd = "help";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        shell_help();
        return;
    }
    
    // Ping command
    match = 1;
    cmd = "ping";
    j = 0;
    while (cmd[j] != '\0' && args[0][j] != '\0') {
        if (cmd[j] != args[0][j]) {
            match = 0;
            break;
        }
        j++;
    }
    if (match && cmd[j] == '\0' && args[0][j] == '\0') {
        if (arg_count < 2) {
            terminal_writestring_color("Usage: ping <ip_address>\n", COLOR_YELLOW);
            terminal_writestring_color("Example: ping 10.0.2.2\n", COLOR_YELLOW);
        } else {
            // Parse IP address (simple format: a.b.c.d)
            uint32_t ip = 0;
            const char* ip_str = args[1];
            int octet = 0;
            int value = 0;
            int valid = 1;
            
            for (int i = 0; ip_str[i] != '\0' && valid; i++) {
                if (ip_str[i] >= '0' && ip_str[i] <= '9') {
                    value = value * 10 + (ip_str[i] - '0');
                    if (value > 255) {
                        valid = 0;
                    }
                } else if (ip_str[i] == '.') {
                    if (octet >= 4 || value > 255) {
                        valid = 0;
                        break;
                    }
                    ip |= (value & 0xFF) << (octet * 8);
                    octet++;
                    value = 0;
                } else {
                    valid = 0;
                }
            }
            
            if (valid && octet == 3) {
                ip |= (value & 0xFF) << (octet * 8);
                
                // Convert to network byte order (IP addresses are stored in network byte order)
                // Our parsing gives us host byte order, but IP layer expects network byte order
                // Actually, IP addresses in the IP header are in network byte order,
                // but we're storing them as uint32_t in host byte order for simplicity.
                // The IP layer will handle the conversion when building the header.
                
                terminal_writestring("Pinging ");
                terminal_writestring(args[1]);
                terminal_writestring("...\n");
                
                // Poll for network packets before sending (to process any pending packets)
                extern void arp_poll(void);
                arp_poll();
                
                // Send ping (4 packets)
                for (int seq = 0; seq < 4; seq++) {
                    // Poll for packets before each ping
                    ethernet_poll_for_packets();
                    
                    // Send echo request
                    if (icmp_send_echo_request(ip, 1, seq, NULL, 0) == 0) {
                        terminal_writestring("Ping sent (seq ");
                        // Print sequence number
                        char seq_str[4];
                        int seq_pos = 0;
                        int temp = seq;
                        if (temp == 0) {
                            seq_str[seq_pos++] = '0';
                        } else {
                            char rev[4];
                            int rev_pos = 0;
                            while (temp > 0) {
                                rev[rev_pos++] = '0' + (temp % 10);
                                temp /= 10;
                            }
                            for (int k = rev_pos - 1; k >= 0; k--) {
                                seq_str[seq_pos++] = rev[k];
                            }
                        }
                        seq_str[seq_pos] = '\0';
                        terminal_writestring(seq_str);
                        terminal_writestring(")\n");
                    } else {
                        terminal_writestring_color("Failed to send ping\n", COLOR_RED);
                    }
                    
                    // Wait and poll for replies
                    for (int wait = 0; wait < 50; wait++) {
                        ethernet_poll_for_packets();
                        // Small delay
                        for (volatile int delay = 0; delay < 20000; delay++);
                    }
                }
                
                // Final poll for any remaining replies
                for (int i = 0; i < 10; i++) {
                    ethernet_poll_for_packets();
                    for (volatile int delay = 0; delay < 20000; delay++);
                }
                
                terminal_writestring("Ping complete.\n");
            } else {
                terminal_writestring_color("Invalid IP address format\n", COLOR_RED);
                terminal_writestring_color("Expected format: a.b.c.d (e.g., 10.0.2.2)\n", COLOR_YELLOW);
            }
        }
        return;
    }
    
    // Check if it's a .bas file - run with BASIC interpreter
    uint32_t arg_len = 0;
    while (args[0][arg_len] != '\0') arg_len++;
    if (arg_len >= 4) {
        // Check if ends with .bas
        if (args[0][arg_len-4] == '.' &&
            args[0][arg_len-3] == 'b' &&
            args[0][arg_len-2] == 'a' &&
            args[0][arg_len-1] == 's') {
            // Resolve path and run with BASIC interpreter
            fs_node_t* file = fs_resolve_path(args[0]);
            if (file != NULL && file->type == FS_FILE) {
                basic_load_and_run(args[0]);
                basic_cleanup();
                return;
            }
        }
    }
    // Check if it's a .bf file - run with Brainfuck interpreter
    if (arg_len >= 3) {
        if (args[0][arg_len-3] == '.' &&
            args[0][arg_len-2] == 'b' &&
            args[0][arg_len-1] == 'f') {
            // Resolve path and run with Brainfuck interpreter
            fs_node_t* file = fs_resolve_path(args[0]);
            if (file != NULL && file->type == FS_FILE) {
                brainfuck_load_and_run(args[0]);
                brainfuck_cleanup();
                return;
            }
        }
    }
    
    // Check if it's a program in /sys/components
    fs_node_t* program = fs_find_program(args[0]);
    if (program != NULL) {
        // Found program in system path - check if it's a .bas or .bf file
        uint32_t name_len = 0;
        while (program->name[name_len] != '\0') name_len++;
        
        int is_bas = 0;
        int is_bf = 0;
        if (name_len >= 4) {
            // Check if ends with .bas
            if (program->name[name_len-4] == '.' &&
                program->name[name_len-3] == 'b' &&
                program->name[name_len-2] == 'a' &&
                program->name[name_len-1] == 's') {
                is_bas = 1;
            }
        }
        if (name_len >= 3) {
            // Check if ends with .bf
            if (program->name[name_len-3] == '.' &&
                program->name[name_len-2] == 'b' &&
                program->name[name_len-1] == 'f') {
                is_bf = 1;
            }
        }
        
        // Build full path
        char program_path[MAX_PATH_LENGTH];
        program_path[0] = '/';
        program_path[1] = 's';
        program_path[2] = 'y';
        program_path[3] = 's';
        program_path[4] = '/';
        program_path[5] = 'c';
        program_path[6] = 'o';
        program_path[7] = 'm';
        program_path[8] = 'p';
        program_path[9] = 'o';
        program_path[10] = 'n';
        program_path[11] = 'e';
        program_path[12] = 'n';
        program_path[13] = 't';
        program_path[14] = 's';
        program_path[15] = '/';
        uint32_t i = 16;
        uint32_t j = 0;
        while (program->name[j] != '\0' && i < MAX_PATH_LENGTH - 1) {
            program_path[i++] = program->name[j++];
        }
        program_path[i] = '\0';
        
        if (is_bas) {
            // Run with BASIC interpreter
            basic_load_and_run(program_path);
            basic_cleanup();
        } else if (is_bf) {
            // Run with Brainfuck interpreter
            brainfuck_load_and_run(program_path);
            brainfuck_cleanup();
        } else {
            // Run as AFOS binary
            exec_load_and_run(program_path, arg_count, args);
        }
        return;
    }
    
    // Unknown command
    terminal_writestring_color("Unknown command: ", COLOR_RED);
    terminal_writestring(args[0]);
    terminal_writestring_color("\n", COLOR_RED);
}

// Run shell - matches inspiration kernel exactly
void shell_run(void) {
    while (1) {
        shell_print_prompt();
        
        shell_buffer_pos = 0;
        shell_buffer[0] = '\0';
        
        while (1) {
            // Aggressively poll keyboard - call multiple times (like inspiration kernel)
            keyboard_handler();
            keyboard_handler(); // Poll twice to catch rapid keystrokes
            
            int c = keyboard_getchar();
            if (c == -1) {
                // Small delay to give CPU time
                for (volatile int i = 0; i < 5000; i++);
                // Don't use HLT - it might prevent keyboard polling on some systems
                continue;
            }
            
            if (c == '\n' || c == '\r') {
                terminal_putchar('\n');
                shell_buffer[shell_buffer_pos] = '\0';
                shell_process_command(shell_buffer);
                break; // Break to show prompt again
            }
            
            if (c == '\b' || c == 127) {
                // Backspace - delete character
                if (shell_buffer_pos > 0) {
                    shell_buffer_pos--;
                    shell_buffer[shell_buffer_pos] = '\0';
                    // terminal_putchar('\b') now handles backspace properly
                    terminal_putchar('\b');
                }
                continue;
            }
            
            // Only accept printable ASCII characters (32-126)
            // Ignore any other characters (like weird control codes)
            if (c >= 32 && c < 127 && shell_buffer_pos < SHELL_BUFFER_SIZE - 1) {
                shell_buffer[shell_buffer_pos++] = (char)c;
                shell_buffer[shell_buffer_pos] = '\0';
                terminal_putchar((char)c);
            }
            // Silently ignore invalid characters (don't display them)
        }
    }
}

// Initialize shell
void shell_init(void) {
    shell_buffer_pos = 0;
    shell_buffer[0] = '\0';
}

