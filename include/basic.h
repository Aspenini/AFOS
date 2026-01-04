#ifndef BASIC_H
#define BASIC_H

#include "types.h"

// Public API
int basic_execute(const char* source);
int basic_load_and_run(const char* path);
void basic_cleanup(void);

#endif

