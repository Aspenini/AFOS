#ifndef IDT_H
#define IDT_H

#include "types.h"

#define IDT_SIZE 256

// IDT entry structure
struct idt_entry {
    uint16_t base_low;      // Lower 16 bits of handler address
    uint16_t selector;     // Code segment selector
    uint8_t  always0;      // Always 0
    uint8_t  flags;        // Flags
    uint16_t base_high;     // Upper 16 bits of handler address
} __attribute__((packed));

// IDT pointer structure
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t selector, uint8_t flags);

#endif

