#ifndef AC97_H
#define AC97_H

#include "types.h"

// AC97 functions
int ac97_init(void);
int ac97_play_pcm(uint8_t* samples, uint32_t sample_count, uint32_t sample_rate);
int ac97_stop(void);
int ac97_is_playing(void);

#endif // AC97_H

