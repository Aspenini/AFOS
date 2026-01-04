#ifndef EXECUTABLE_H
#define EXECUTABLE_H

#include "types.h"

// AFOS Executable Format
// Simple binary format for compiled programs

#define AFOS_EXEC_MAGIC 0x534F4641  // "AFOS" in little-endian
#define AFOS_EXEC_VERSION 1

// Executable header (20 bytes)
struct afos_exec_header {
    uint32_t magic;        // "AFOS" magic number
    uint8_t  version;      // Format version
    uint8_t  reserved[3];  // Reserved for future use
    uint32_t entry_point;  // Offset from start of code section
    uint32_t code_size;    // Size of code section
    uint32_t data_size;    // Size of data section (for future use)
} __attribute__((packed));

// Function pointer type for executables
typedef int (*exec_entry_t)(int argc, char** argv);

// Load and execute an AFOS binary
int exec_load_and_run(const char* path, int argc, char** argv);

// Check if a file is an AFOS executable
int exec_is_valid(const uint8_t* data, uint32_t size);

#endif

