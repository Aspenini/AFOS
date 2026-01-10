#ifndef PIT_H
#define PIT_H

#include "types.h"

// PIT (Programmable Interval Timer) functions
int pit_init(uint32_t frequency_hz);
uint32_t pit_get_ticks(void);
void pit_sleep_ms(uint32_t ms);
void pit_timer_handler(void);  // Called from ISR

#endif // PIT_H

