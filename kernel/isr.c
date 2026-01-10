#include "isr.h"
#include "keyboard.h"
#include "pit.h"
#include "types.h"

// Array of ISR handlers
static isr_t isr_handlers[256];

// Default ISR handler
void isr_handler(void) {
    // Handle exceptions if needed
}

// IRQ handler - receives IRQ number as parameter (pushed by assembly stub)
void irq_handler(uint32_t irq_num) {
    // Handle specific IRQ
    if (irq_num >= 32 && irq_num <= 47) {
        uint8_t irq = irq_num - 32;
        
        // Handle timer (IRQ0)
        if (irq == 0) {
            extern void pit_timer_handler(void);
            pit_timer_handler();
        }
        
        // Handle keyboard (IRQ1)
        if (irq == 1) {
            keyboard_handler();
        }
        
        // Handle network card (IRQ11 - typical for RTL8139)
        if (irq == 11) {
            extern void rtl8139_irq_handler(void);
            rtl8139_irq_handler();
        }
        
        // Send EOI to PIC (must be done before handling to allow nested interrupts)
        if (irq >= 8) {
            // Send to slave PIC
            __asm__ volatile("mov $0x20, %%al\n\toutb %%al, $0xA0" ::: "al");
        }
        // Send to master PIC
        __asm__ volatile("mov $0x20, %%al\n\toutb %%al, $0x20" ::: "al");
    }
}

// Register an ISR handler
void isr_register_handler(uint8_t num, isr_t handler) {
    if (num < 256) {
        isr_handlers[num] = handler;
    }
}

// Initialize PIC (Programmable Interrupt Controller)
void pic_init(void) {
    // Save masks
    uint8_t a1, a2;
    __asm__ volatile("inb $0x21, %0" : "=a"(a1));
    __asm__ volatile("inb $0xA1, %0" : "=a"(a2));
    
    // Initialize master PIC
    __asm__ volatile(
        "mov $0x11, %%al\n\t"
        "out %%al, $0x20\n\t"
        "mov $0x20, %%al\n\t"
        "out %%al, $0x21\n\t"
        "mov $0x04, %%al\n\t"
        "out %%al, $0x21\n\t"
        "mov $0x01, %%al\n\t"
        "out %%al, $0x21\n\t"
        :
        :
        : "al"
    );
    
    // Initialize slave PIC
    __asm__ volatile(
        "mov $0x11, %%al\n\t"
        "out %%al, $0xA0\n\t"
        "mov $0x28, %%al\n\t"
        "out %%al, $0xA1\n\t"
        "mov $0x02, %%al\n\t"
        "out %%al, $0xA1\n\t"
        "mov $0x01, %%al\n\t"
        "out %%al, $0xA1\n\t"
        :
        :
        : "al"
    );
    
    // Mask all interrupts initially
    __asm__ volatile("mov $0xFF, %%al\n\tout %%al, $0x21\n\t" ::: "al");
    __asm__ volatile("mov $0xFF, %%al\n\tout %%al, $0xA1\n\t" ::: "al");
    
    // Enable IRQ0 (timer) - clear bit 0
    __asm__ volatile(
        "inb $0x21, %%al\n\t"
        "and $0xFE, %%al\n\t"  // Clear bit 0 (timer)
        "out %%al, $0x21\n\t"
        :
        :
        : "al"
    );
    
    // Enable IRQ1 (keyboard) - clear bit 1
    __asm__ volatile(
        "inb $0x21, %%al\n\t"
        "and $0xFD, %%al\n\t"  // Clear bit 1 (keyboard)
        "out %%al, $0x21\n\t"
        :
        :
        : "al"
    );
    
    // Enable IRQ11 (network card) - clear bit 3 in slave PIC
    __asm__ volatile(
        "inb $0xA1, %%al\n\t"
        "and $0xF7, %%al\n\t"  // Clear bit 3 (IRQ11)
        "out %%al, $0xA1\n\t"
        :
        :
        : "al"
    );
}

