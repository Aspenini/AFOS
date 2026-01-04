#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Keyboard buffer
#define KEYBOARD_BUFFER_SIZE 256

void keyboard_init(void);
void keyboard_handler(void);
int keyboard_getchar(void); // Returns -1 if no data available
int keyboard_has_input(void);
void keyboard_clear_buffer(void);

#endif

