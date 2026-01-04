// Simple memory allocator for kernel
// Very basic implementation using a static pool

#include "types.h"

#define MALLOC_POOL_SIZE (2 * 1024 * 1024)  // 2MB for dynamic allocation (supports graphics framebuffer)
static uint8_t malloc_pool[MALLOC_POOL_SIZE];
static uint32_t malloc_used = 0;

// Simple malloc implementation
void* malloc(uint32_t size) {
    if (size == 0) {
        return NULL;
    }
    
    // Align to 4 bytes
    size = (size + 3) & ~3;
    
    if (malloc_used + size > MALLOC_POOL_SIZE) {
        return NULL;
    }
    
    void* ptr = &malloc_pool[malloc_used];
    malloc_used += size;
    
    return ptr;
}

// Simple free implementation (does nothing - no deallocation)
// In a real OS, you'd implement proper free list management
void free(void* ptr) {
    // For now, we don't free memory
    // This is acceptable for a simple kernel where programs run and exit
    (void)ptr;  // Suppress unused parameter warning
}

// Reset allocator (useful for cleaning up between program runs)
void malloc_reset(void) {
    malloc_used = 0;
}

