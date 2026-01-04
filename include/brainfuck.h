#ifndef BRAINFUCK_H
#define BRAINFUCK_H

#include "types.h"

// Public API
int brainfuck_execute(const char* source);
int brainfuck_load_and_run(const char* path);
void brainfuck_cleanup(void);

#endif

