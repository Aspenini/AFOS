#ifndef WAV_H
#define WAV_H

#include "types.h"

// WAV file structure
typedef struct {
    uint8_t* data;          // Raw WAV file data
    uint32_t data_size;     // Total file size
    
    // PCM data
    uint8_t* pcm_data;      // Pointer to PCM data in file
    uint32_t pcm_size;       // Size of PCM data in bytes
    
    // Audio format
    uint16_t audio_format;   // 1 = PCM
    uint16_t num_channels;   // 1 = mono, 2 = stereo
    uint32_t sample_rate;   // Samples per second
    uint16_t bits_per_sample; // 8, 16, etc.
    uint16_t block_align;    // Bytes per sample frame
    uint32_t byte_rate;      // Bytes per second
    
    int valid;               // 1 if WAV file is valid
} wav_file_t;

// Parse WAV file from memory
int wav_parse(uint8_t* data, uint32_t size, wav_file_t* wav);

// Convert WAV samples to 8-bit unsigned mono (for AC97)
// Returns number of output samples
uint32_t wav_convert_to_8bit_mono(wav_file_t* wav, uint8_t* output_buffer, uint32_t max_samples);

#endif // WAV_H

