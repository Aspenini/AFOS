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

