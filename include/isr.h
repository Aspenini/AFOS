#ifndef ISR_H
#define ISR_H

#include "types.h"

// ISR function pointer type
typedef void (*isr_t)(void);

// Register interrupt handler
void isr_register_handler(uint8_t num, isr_t handler);

// Initialize PIC
void pic_init(void);

// IRQ numbers
#define IRQ0  32
#define IRQ1  33  // Keyboard
#define IRQ2  34
#define IRQ3  35
#define IRQ4  36
#define IRQ5  37
#define IRQ6  38
#define IRQ7  39
#define IRQ8  40
#define IRQ9  41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

#endif

