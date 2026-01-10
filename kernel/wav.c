#include "wav.h"
#include "types.h"

// Forward declarations
void terminal_writestring(const char* data);
void terminal_writestring_color(const char* data, uint8_t color);
void* malloc(uint32_t size);
void free(void* ptr);

#define COLOR_RED 0x0C
#define COLOR_GREEN 0x0A
#define COLOR_YELLOW 0x0E

// WAV file chunk IDs
#define WAV_RIFF_ID 0x46464952  // "RIFF" in little-endian
#define WAV_WAVE_ID 0x45564157  // "WAVE" in little-endian
#define WAV_FMT_ID  0x20746D66  // "fmt " in little-endian
#define WAV_DATA_ID 0x61746164  // "data" in little-endian

// Read 16-bit little-endian
static uint16_t read_le16(uint8_t* data) {
    return data[0] | (data[1] << 8);
}

// Read 32-bit little-endian
static uint32_t read_le32(uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

// Parse WAV file
int wav_parse(uint8_t* data, uint32_t size, wav_file_t* wav) {
    if (data == NULL || size < 12 || wav == NULL) {
        return -1;
    }
    
    // Initialize
    wav->data = data;
    wav->data_size = size;
    wav->pcm_data = NULL;
    wav->pcm_size = 0;
    wav->valid = 0;
    
    // Check RIFF header
    if (read_le32(data) != WAV_RIFF_ID) {
        terminal_writestring_color("WAV: Not a RIFF file\n", COLOR_RED);
        return -1;
    }
    
    // Skip file size (4 bytes)
    uint32_t file_size = read_le32(data + 4);
    
    // Check WAVE ID
    if (read_le32(data + 8) != WAV_WAVE_ID) {
        terminal_writestring_color("WAV: Not a WAVE file\n", COLOR_RED);
        return -1;
    }
    
    // Parse chunks
    uint32_t offset = 12;
    int found_fmt = 0;
    int found_data = 0;
    
    while (offset < size - 8) {
        uint32_t chunk_id = read_le32(data + offset);
        uint32_t chunk_size = read_le32(data + offset + 4);
        offset += 8;
        
        if (chunk_id == WAV_FMT_ID) {
            // Format chunk
            if (chunk_size < 16) {
                terminal_writestring_color("WAV: Format chunk too small\n", COLOR_RED);
                return -1;
            }
            
            wav->audio_format = read_le16(data + offset);
            wav->num_channels = read_le16(data + offset + 2);
            wav->sample_rate = read_le32(data + offset + 4);
            wav->byte_rate = read_le32(data + offset + 8);
            wav->block_align = read_le16(data + offset + 12);
            wav->bits_per_sample = read_le16(data + offset + 14);
            
            found_fmt = 1;
            
            // Support PCM (format 1) and IEEE float (format 3)
            if (wav->audio_format != 1 && wav->audio_format != 3) {
                terminal_writestring("WAV: Warning - Non-PCM format (");
                // Print format code
                char fmt_str[8];
                int temp = wav->audio_format;
                int pos = 0;
                if (temp == 0) {
                    fmt_str[pos++] = '0';
                } else {
                    char rev[8];
                    int rev_pos = 0;
                    while (temp > 0) {
                        rev[rev_pos++] = '0' + (temp % 10);
                        temp /= 10;
                    }
                    for (int k = rev_pos - 1; k >= 0; k--) {
                        fmt_str[pos++] = rev[k];
                    }
                }
                fmt_str[pos] = '\0';
                terminal_writestring(fmt_str);
                terminal_writestring("). Only PCM (1) and IEEE float (3) supported.\n");
                return -1;
            }
            
        } else if (chunk_id == WAV_DATA_ID) {
            // Data chunk
            wav->pcm_data = data + offset;
            wav->pcm_size = chunk_size;
            found_data = 1;
        }
        
        // Move to next chunk (chunk size is aligned to even bytes)
        offset += chunk_size;
        if (chunk_size % 2 != 0) {
            offset++;  // Skip padding byte
        }
    }
    
    if (!found_fmt) {
        terminal_writestring_color("WAV: Format chunk not found\n", COLOR_RED);
        return -1;
    }
    
    if (!found_data) {
        terminal_writestring_color("WAV: Data chunk not found\n", COLOR_RED);
        return -1;
    }
    
    wav->valid = 1;
    return 0;
}

// Read 32-bit float (IEEE 754)
static float read_float32(uint8_t* data) {
    union {
        uint32_t i;
        float f;
    } u;
    u.i = read_le32(data);
    return u.f;
}

// Convert WAV samples to 8-bit unsigned mono
uint32_t wav_convert_to_8bit_mono(wav_file_t* wav, uint8_t* output_buffer, uint32_t max_samples) {
    if (!wav->valid || wav->pcm_data == NULL || output_buffer == NULL) {
        return 0;
    }
    
    uint32_t input_samples = wav->pcm_size / wav->block_align;
    uint32_t output_samples = input_samples;
    
    // Limit output samples
    if (output_samples > max_samples) {
        output_samples = max_samples;
    }
    
    // Handle IEEE float format (format 3)
    if (wav->audio_format == 3) {
        // IEEE float samples
        if (wav->bits_per_sample == 32) {
            if (wav->num_channels == 1) {
                // 32-bit float mono
                for (uint32_t i = 0; i < output_samples; i++) {
                    float sample = read_float32(wav->pcm_data + i * 4);
                    // Clamp to [-1.0, 1.0] and convert to 0-255
                    if (sample < -1.0f) sample = -1.0f;
                    if (sample > 1.0f) sample = 1.0f;
                    int32_t scaled = (int32_t)((sample + 1.0f) * 127.5f);
                    if (scaled < 0) scaled = 0;
                    if (scaled > 255) scaled = 255;
                    output_buffer[i] = (uint8_t)scaled;
                }
            } else {
                // 32-bit float multi-channel - average all channels
                for (uint32_t i = 0; i < output_samples; i++) {
                    float sum = 0.0f;
                    for (uint16_t ch = 0; ch < wav->num_channels; ch++) {
                        float sample = read_float32(wav->pcm_data + i * wav->block_align + ch * 4);
                        sum += sample;
                    }
                    float avg = sum / wav->num_channels;
                    // Clamp and convert
                    if (avg < -1.0f) avg = -1.0f;
                    if (avg > 1.0f) avg = 1.0f;
                    int32_t scaled = (int32_t)((avg + 1.0f) * 127.5f);
                    if (scaled < 0) scaled = 0;
                    if (scaled > 255) scaled = 255;
                    output_buffer[i] = (uint8_t)scaled;
                }
            }
        } else {
            terminal_writestring_color("WAV: Unsupported float bit depth\n", COLOR_RED);
            return 0;
        }
        return output_samples;
    }
    
    // Convert based on bit depth and channels (PCM format)
    // Handle any number of channels by averaging
    uint32_t bytes_per_sample = wav->bits_per_sample / 8;
    uint32_t bytes_per_frame = wav->block_align;
    
    if (wav->bits_per_sample == 8) {
        // 8-bit samples (unsigned)
        if (wav->num_channels == 1) {
            // Already 8-bit mono - just copy
            for (uint32_t i = 0; i < output_samples; i++) {
                output_buffer[i] = wav->pcm_data[i];
            }
        } else {
            // 8-bit multi-channel - average all channels
            for (uint32_t i = 0; i < output_samples; i++) {
                uint32_t sum = 0;
                for (uint16_t ch = 0; ch < wav->num_channels; ch++) {
                    sum += wav->pcm_data[i * bytes_per_frame + ch];
                }
                output_buffer[i] = (uint8_t)(sum / wav->num_channels);
            }
        }
    } else if (wav->bits_per_sample == 16) {
        // 16-bit samples (signed)
        if (wav->num_channels == 1) {
            // 16-bit mono - convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                int16_t sample = (int16_t)read_le16(wav->pcm_data + i * 2);
                // Convert signed 16-bit (-32768 to 32767) to unsigned 8-bit (0 to 255)
                int32_t scaled = ((int32_t)sample + 32768) * 255 / 65536;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        } else {
            // 16-bit multi-channel - average all channels and convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < wav->num_channels; ch++) {
                    int16_t sample = (int16_t)read_le16(wav->pcm_data + i * bytes_per_frame + ch * 2);
                    sum += sample;
                }
                int16_t avg = (int16_t)(sum / wav->num_channels);
                // Convert signed 16-bit to unsigned 8-bit
                int32_t scaled = ((int32_t)avg + 32768) * 255 / 65536;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        }
    } else if (wav->bits_per_sample == 24) {
        // 24-bit samples (signed, stored as 3 bytes)
        if (wav->num_channels == 1) {
            // 24-bit mono - convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                // Read 24-bit signed sample (little-endian)
                uint8_t* sample_ptr = wav->pcm_data + i * 3;
                int32_t sample = (int32_t)(sample_ptr[0] | (sample_ptr[1] << 8) | (sample_ptr[2] << 16));
                // Sign extend from 24-bit to 32-bit
                if (sample & 0x800000) {
                    sample |= 0xFF000000;
                }
                // Convert signed 24-bit to unsigned 8-bit
                int32_t scaled = ((sample + 8388608) * 255 / 16777216);
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        } else {
            // 24-bit multi-channel - average all channels and convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < wav->num_channels; ch++) {
                    uint8_t* sample_ptr = wav->pcm_data + i * bytes_per_frame + ch * 3;
                    int32_t sample = (int32_t)(sample_ptr[0] | (sample_ptr[1] << 8) | (sample_ptr[2] << 16));
                    // Sign extend
                    if (sample & 0x800000) {
                        sample |= 0xFF000000;
                    }
                    sum += sample;
                }
                int32_t avg = sum / wav->num_channels;
                // Convert signed 24-bit to unsigned 8-bit
                int32_t scaled = ((avg + 8388608) * 255 / 16777216);
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        }
    } else if (wav->bits_per_sample == 32) {
        // 32-bit samples (signed)
        if (wav->num_channels == 1) {
            // 32-bit mono - convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                int32_t sample = (int32_t)read_le32(wav->pcm_data + i * 4);
                // Convert signed 32-bit to unsigned 8-bit
                // Scale from -2147483648..2147483647 to 0..255
                int64_t scaled = ((int64_t)sample + 2147483648LL) * 255 / 4294967296LL;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        } else {
            // 32-bit multi-channel - average all channels and convert to 8-bit
            for (uint32_t i = 0; i < output_samples; i++) {
                int64_t sum = 0;
                for (uint16_t ch = 0; ch < wav->num_channels; ch++) {
                    int32_t sample = (int32_t)read_le32(wav->pcm_data + i * bytes_per_frame + ch * 4);
                    sum += sample;
                }
                int32_t avg = (int32_t)(sum / wav->num_channels);
                // Convert signed 32-bit to unsigned 8-bit
                int64_t scaled = ((int64_t)avg + 2147483648LL) * 255 / 4294967296LL;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        }
    } else {
        // Unsupported bit depth - try to handle gracefully
        terminal_writestring("WAV: Unsupported bit depth (");
        // Print bit depth
        char bits_str[8];
        int temp = wav->bits_per_sample;
        int pos = 0;
        if (temp == 0) {
            bits_str[pos++] = '0';
        } else {
            char rev[8];
            int rev_pos = 0;
            while (temp > 0) {
                rev[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int k = rev_pos - 1; k >= 0; k--) {
                bits_str[pos++] = rev[k];
            }
        }
        bits_str[pos] = '\0';
        terminal_writestring(bits_str);
        terminal_writestring(" bits). Attempting conversion...\n");
        
        // Try to convert anyway by treating as 16-bit (may not be accurate)
        // This is a fallback for unusual bit depths
        if (wav->num_channels == 1) {
            for (uint32_t i = 0; i < output_samples; i++) {
                // Read first 16 bits and treat as signed
                int16_t sample = (int16_t)read_le16(wav->pcm_data + i * bytes_per_frame);
                int32_t scaled = ((int32_t)sample + 32768) * 255 / 65536;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        } else {
            for (uint32_t i = 0; i < output_samples; i++) {
                int32_t sum = 0;
                for (uint16_t ch = 0; ch < wav->num_channels && ch < 8; ch++) {  // Limit to 8 channels
                    int16_t sample = (int16_t)read_le16(wav->pcm_data + i * bytes_per_frame + ch * bytes_per_sample);
                    sum += sample;
                }
                int16_t avg = (int16_t)(sum / wav->num_channels);
                int32_t scaled = ((int32_t)avg + 32768) * 255 / 65536;
                if (scaled < 0) scaled = 0;
                if (scaled > 255) scaled = 255;
                output_buffer[i] = (uint8_t)scaled;
            }
        }
    }
    
    return output_samples;
}

