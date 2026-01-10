#ifndef SB16_H
#define SB16_H

#include "types.h"

// Sound Blaster 16 functions
int sb16_init(void);
int sb16_play_pcm(uint8_t* samples, uint32_t sample_count, uint32_t sample_rate);
int sb16_stop(void);
int sb16_is_playing(void);

#endif // SB16_H

