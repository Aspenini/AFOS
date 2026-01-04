#include "keyboard.h"
#include "types.h"

// Keyboard buffer
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static uint32_t keyboard_buffer_head = 0;
static uint32_t keyboard_buffer_tail = 0;
static uint32_t keyboard_buffer_count = 0;

// Shift key state
static int shift_pressed = 0;
// Control key state
static int ctrl_pressed = 0;

// Scan code to ASCII conversion (US QWERTY layout)
// Handles both scan code set 1 and set 2
// Set 1: 0x01-0x58, break codes use 0xF0 prefix
// Set 2: 0x01-0x58, break codes are 0x80+
static char scan_code_to_ascii[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// Scan code to ASCII with shift (uppercase and symbols)
static char scan_code_to_ascii_shift[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// Read keyboard status port
static inline uint8_t keyboard_read_status(void) {
    uint8_t status;
    __asm__ volatile("inb %1, %0" : "=a"(status) : "Nd"(KEYBOARD_STATUS_PORT));
    return status;
}

// Read keyboard data port
static inline uint8_t keyboard_read_data(void) {
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "Nd"(KEYBOARD_DATA_PORT));
    return data;
}

// Check if keyboard has data available
static int keyboard_has_data(void) {
    uint8_t status = keyboard_read_status();
    // Bit 0 = output buffer full (data ready to read)
    // Bit 1 = input buffer full (don't write when this is set)
    // Bit 5 = timeout/parity error (ignore if set, might be false positive)
    return (status & 0x01) != 0;
}

// Write to keyboard command port
static inline void keyboard_write_command(uint8_t cmd) {
    // Wait for input buffer to be empty
    while (keyboard_read_status() & 0x02) {
        // Input buffer full, wait
    }
    __asm__ volatile("outb %0, %1" : : "a"(cmd), "Nd"(KEYBOARD_STATUS_PORT));
}

// Initialize keyboard
void keyboard_init(void) {
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    keyboard_buffer_count = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
    
    // Initialize PS/2 keyboard
    keyboard_write_command(0xAE);
    
    // Clear any pending keyboard data
    int clear_count = 0;
    while (keyboard_has_data() && clear_count++ < 10) {
        keyboard_read_data(); // Discard any stale data
    }
}

// Handle keyboard interrupt (polling version) - matches inspiration kernel exactly
void keyboard_handler(void) {
    // PS/2 mode: handle scan codes
    // Poll keyboard multiple times to catch all pending keys
    int max_polls = 10;
    static int key_released = 0;
    
    while (keyboard_has_data() && max_polls-- > 0) {
        uint8_t scancode = keyboard_read_data();
        
        // Handle scan code set 2 break codes (0x80+)
        if (scancode >= 0x80) {
            // Check for modifier key release
            if (scancode == 0xAA || scancode == 0xB6) { // Left/Right Shift break
                shift_pressed = 0;
            } else if (scancode == 0x9D || scancode == 0xB8) { // Left/Right Ctrl break
                ctrl_pressed = 0;
            }
            continue; // Ignore other break codes
        }
        
        // Handle scan code set 1 key release (0xF0 prefix)
        if (scancode == 0xF0) {
            key_released = 1;
            continue;
        }
        
        // Skip break codes (key release in set 1)
        if (key_released) {
            // Check for modifier key release
            if (scancode == 0x2A || scancode == 0x36) { // Left/Right Shift
                shift_pressed = 0;
            } else if (scancode == 0x1D) { // Left/Right Ctrl (same code, extended bit differs)
                ctrl_pressed = 0;
            }
            key_released = 0;
            continue;
        }
        
        // Handle modifier key press
        if (scancode == 0x2A || scancode == 0x36) { // Left Shift (0x2A) or Right Shift (0x36)
            shift_pressed = 1;
            continue;
        }
        
        if (scancode == 0x1D) { // Left Ctrl
            ctrl_pressed = 1;
            continue;
        }
        
        // Convert scan code to ASCII (both sets use 0x01-0x58 for make codes)
        if (scancode > 0 && scancode < 128) {
            char ascii;
            if (shift_pressed) {
                ascii = scan_code_to_ascii_shift[scancode];
            } else {
                ascii = scan_code_to_ascii[scancode];
            }
            
            // Handle Ctrl+Q (quit) - send special code 0x11 (Ctrl+Q)
            if (ctrl_pressed && ascii == 'q') {
                ascii = 0x11; // Ctrl+Q
            } else if (ctrl_pressed && ascii != 0) {
                // Suppress other Ctrl+key combinations (except Ctrl+Q)
                continue;
            }
            
            // Only add valid printable characters, control characters (like \b, \n, \t), or special codes
            // Valid: 0x08 (\b), 0x09 (\t), 0x0A (\n), 0x0D (\r), 0x11 (Ctrl+Q), or 32-126 (printable)
            if (ascii != 0 && keyboard_buffer_count < KEYBOARD_BUFFER_SIZE) {
                // Only allow valid characters
                if (ascii == '\b' || ascii == '\t' || ascii == '\n' || ascii == '\r' || 
                    ascii == 0x11 || (ascii >= 32 && ascii < 127)) {
                    keyboard_buffer[keyboard_buffer_tail] = ascii;
                    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
                    keyboard_buffer_count++;
                }
            }
        }
    }
}

// Get a character from keyboard buffer (non-blocking)
int keyboard_getchar(void) {
    // Don't call keyboard_handler here - let caller do it to avoid double polling
    
    if (keyboard_buffer_count == 0) {
        return -1; // No data available
    }
    
    char c = keyboard_buffer[keyboard_buffer_head];
    keyboard_buffer_head = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    keyboard_buffer_count--;
    
    return (int)(unsigned char)c;
}

// Check if input is available
int keyboard_has_input(void) {
    return keyboard_buffer_count > 0;
}

// Clear keyboard buffer
void keyboard_clear_buffer(void) {
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    keyboard_buffer_count = 0;
}

