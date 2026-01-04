/*
 * AFOS Binary Packer
 * 
 * Converts a raw binary file into AFOS executable format
 * Usage: afos-pack <input.bin> <output.afos> [entry_offset]
 * 
 * entry_offset defaults to 0 (start of code)
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define AFOS_EXEC_MAGIC 0x534F4641  // "AFOS" in little-endian
#define AFOS_EXEC_VERSION 1

struct afos_exec_header {
    uint32_t magic;
    uint8_t  version;
    uint8_t  reserved[3];
    uint32_t entry_point;
    uint32_t code_size;
    uint32_t data_size;
} __attribute__((packed));

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input.bin> <output.afos> [entry_offset]\n", argv[0]);
        return 1;
    }
    
    FILE* input = fopen(argv[1], "rb");
    if (!input) {
        perror("Failed to open input file");
        return 1;
    }
    
    FILE* output = fopen(argv[2], "wb");
    if (!output) {
        perror("Failed to open output file");
        fclose(input);
        return 1;
    }
    
    // Get file size
    fseek(input, 0, SEEK_END);
    long file_size = ftell(input);
    fseek(input, 0, SEEK_SET);
    
    if (file_size < 0) {
        fprintf(stderr, "Failed to determine file size\n");
        fclose(input);
        fclose(output);
        return 1;
    }
    
    // Read input file
    uint8_t* code = malloc(file_size);
    if (!code) {
        fprintf(stderr, "Failed to allocate memory\n");
        fclose(input);
        fclose(output);
        return 1;
    }
    
    size_t read = fread(code, 1, file_size, input);
    fclose(input);
    
    if (read != (size_t)file_size) {
        fprintf(stderr, "Failed to read entire file\n");
        free(code);
        fclose(output);
        return 1;
    }
    
    // Parse entry offset (default 0)
    uint32_t entry_offset = 0;
    if (argc >= 4) {
        entry_offset = (uint32_t)strtoul(argv[3], NULL, 0);
    }
    
    if (entry_offset >= (uint32_t)file_size) {
        fprintf(stderr, "Entry offset exceeds file size\n");
        free(code);
        fclose(output);
        return 1;
    }
    
    // Create header
    struct afos_exec_header header;
    header.magic = AFOS_EXEC_MAGIC;
    header.version = AFOS_EXEC_VERSION;
    header.reserved[0] = 0;
    header.reserved[1] = 0;
    header.reserved[2] = 0;
    header.entry_point = entry_offset;
    header.code_size = (uint32_t)file_size;
    header.data_size = 0;
    
    // Write header
    if (fwrite(&header, sizeof(header), 1, output) != 1) {
        perror("Failed to write header");
        free(code);
        fclose(output);
        return 1;
    }
    
    // Write code
    if (fwrite(code, file_size, 1, output) != 1) {
        perror("Failed to write code");
        free(code);
        fclose(output);
        return 1;
    }
    
    free(code);
    fclose(output);
    
    printf("Created AFOS executable: %s\n", argv[2]);
    printf("  Code size: %ld bytes\n", file_size);
    printf("  Entry point: 0x%x\n", entry_offset);
    
    return 0;
}

