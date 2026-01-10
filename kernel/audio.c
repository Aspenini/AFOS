#include "audio.h"
#include "ac97.h"
#include "types.h"

// Forward declarations
void* malloc(uint32_t size);
void free(void* ptr);

// Generate a sine wave into buffer
int audio_generate_sine_wave(uint8_t* buffer, uint32_t sample_count, uint32_t frequency_hz, uint32_t sample_rate, uint8_t amplitude) {
    if (buffer == NULL || sample_count == 0) {
        return -1;
    }
    
    // Generate sine wave samples
    for (uint32_t i = 0; i < sample_count; i++) {
        // Calculate phase: 2π * frequency * time
        // time = i / sample_rate
        // phase = 2π * frequency * i / sample_rate
        double phase = 2.0 * 3.14159265358979323846 * frequency_hz * i / sample_rate;
        
        // Calculate sine value (-1 to 1)
        double sine_value = 0.0;
        
        // Simple sine approximation using Taylor series (for environments without math.h)
        // sin(x) ≈ x - x³/6 + x⁵/120 - x⁷/5040
        double x = phase;
        // Normalize to [-π, π] using modulo
        int cycles = (int)(x / (2.0 * 3.14159265358979323846));
        x = x - (cycles * 2.0 * 3.14159265358979323846);
        if (x > 3.14159265358979323846) x -= 2.0 * 3.14159265358979323846;
        if (x < -3.14159265358979323846) x += 2.0 * 3.14159265358979323846;
        
        double x2 = x * x;
        double x3 = x2 * x;
        double x5 = x3 * x2;
        double x7 = x5 * x2;
        
        sine_value = x - (x3 / 6.0) + (x5 / 120.0) - (x7 / 5040.0);
        
        // Convert to 8-bit unsigned (0-255)
        // Sine is -1 to 1, so we add 1 to get 0-2, then scale to 0-255
        double normalized = (sine_value + 1.0) * 0.5;  // 0 to 1
        int sample_int = (int)(normalized * amplitude);
        if (sample_int < 0) sample_int = 0;
        if (sample_int > 255) sample_int = 255;
        
        // Center around 128 for unsigned 8-bit PCM
        buffer[i] = (uint8_t)(128 + (sample_int - 128));
    }
    
    return 0;
}

// Generate and play a tone
int audio_generate_tone(uint32_t frequency_hz, uint32_t duration_ms, uint32_t sample_rate) {
    if (frequency_hz == 0 || duration_ms == 0 || sample_rate == 0) {
        return -1;
    }
    
    // Calculate number of samples needed
    uint32_t sample_count = (sample_rate * duration_ms) / 1000;
    
    // Allocate buffer
    uint8_t* buffer = malloc(sample_count);
    if (buffer == NULL) {
        return -1;
    }
    
    // Generate sine wave
    if (audio_generate_sine_wave(buffer, sample_count, frequency_hz, sample_rate, 127) != 0) {
        free(buffer);
        return -1;
    }
    
    // Play through AC97
    int result = ac97_play_pcm(buffer, sample_count, sample_rate);
    
    // Free buffer
    free(buffer);
    
    return result;
}

