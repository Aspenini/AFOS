#ifndef SHELL_H
#define SHELL_H

#include "types.h"

#define SHELL_BUFFER_SIZE 256
#define SHELL_MAX_ARGS 16

void shell_init(void);
void shell_run(void);
void shell_process_command(const char* input);
void shell_print_prompt(void);

#endif

