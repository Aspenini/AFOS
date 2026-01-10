#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"

// Audio generation functions
int audio_generate_tone(uint32_t frequency_hz, uint32_t duration_ms, uint32_t sample_rate);
int audio_generate_sine_wave(uint8_t* buffer, uint32_t sample_count, uint32_t frequency_hz, uint32_t sample_rate, uint8_t amplitude);

#endif // AUDIO_H

