#include "lupin_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];
    char subchunk1_id[4];
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_header_fmt_t;

// A minimal pseudo-ALSA interface that writes to a mock file if no actual /dev/snd is found.
static int mock_write_pcm(const void *pcm_data, size_t data_size, int append) {
    FILE *f = fopen(LUPIN_MOCK_PCM_FILE, append ? "ab" : "wb");
    if (!f) return -1;
    fwrite(pcm_data, 1, data_size, f);
    fclose(f);
    return 0;
}

static int play_pcm_chunk(const void *pcm_data, size_t data_size, int fd, int is_mock) {
    if (is_mock) {
        return mock_write_pcm(pcm_data, data_size, 1);
    }
    ssize_t written = 0;
    while ((size_t)written < data_size) {
        ssize_t res = write(fd, (const char *)pcm_data + written, data_size - written);
        if (res < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        written += res;
    }
    return 0;
}

int lupin_music_play_file(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    char riff[4];
    uint32_t rsize;
    char wave[4];
    
    if (fread(riff, 1, 4, f) != 4 || fread(&rsize, 1, 4, f) != 4 || fread(wave, 1, 4, f) != 4) {
        fclose(f);
        return -1;
    }
    
    if (strncmp(riff, "RIFF", 4) != 0 || strncmp(wave, "WAVE", 4) != 0) {
        fclose(f);
        return -1;
    }

    char chunk_id[4];
    uint32_t chunk_size;
    long data_pos = -1;
    wav_header_fmt_t fmt;
    int fmt_found = 0;

    while (fread(chunk_id, 1, 4, f) == 4 && fread(&chunk_size, 1, 4, f) == 4) {
        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size >= 16) {
                // Read the rest of fmt chunk (AudioFormat, NumChannels, SampleRate, ByteRate, BlockAlign, BitsPerSample)
                if (fread(&fmt.audio_format, 1, 16, f) != 16) break;
                fmt_found = 1;
                long remain = chunk_size - 16;
                if (remain > 0) fseek(f, remain, SEEK_CUR);
            } else {
                fseek(f, chunk_size, SEEK_CUR);
            }
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            data_pos = ftell(f);
            break;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }
    
    if (data_pos < 0 || !fmt_found) {
        fclose(f);
        return -1;
    }
    
    const char *dev = getenv("LUPIN_PCM_DEVICE");
    if (!dev) dev = LUPIN_DEFAULT_PCM_DEVICE;
    
    int is_mock = 0;
    int fd = open(dev, O_WRONLY);
    if (fd < 0) {
        is_mock = 1;
        // Truncate mock file
        FILE *mf = fopen(LUPIN_MOCK_PCM_FILE, "wb");
        if (mf) fclose(mf);
    }
    
    fseek(f, data_pos, SEEK_SET);
    
    // Chunk size: 4096 frames
    size_t frame_size = fmt.num_channels * (fmt.bits_per_sample / 8);
    size_t chunk_bytes = 4096 * frame_size;
    void *buffer = malloc(chunk_bytes);
    if (!buffer) {
        if (!is_mock) close(fd);
        fclose(f);
        return -1;
    }
    
    size_t bytes_read;
    int ret = 0;
    while ((bytes_read = fread(buffer, 1, chunk_bytes, f)) > 0) {
        play_pcm_chunk(buffer, bytes_read, fd, is_mock);
    }
    
    free(buffer);
    if (!is_mock) close(fd);
    fclose(f);
    
    return ret;
}

// ADSR envelope helper
static double adsr(double t, double total_len) {
    double a = 0.05;
    double d = 0.1;
    double s = 0.7;
    double r = 0.2;
    if (t < a) return t / a;
    if (t < a + d) return 1.0 - (1.0 - s) * ((t - a) / d);
    if (t < total_len - r) return s;
    if (t < total_len) return s * (total_len - t) / r;
    return 0.0;
}

int lupin_music_generate_theme(int sample_rate, int16_t **out_buffer, size_t *out_samples) {
    if (sample_rate <= 0 || !out_buffer || !out_samples) return -1;
    
    double duration = 12.0; // 12 seconds
    size_t num_frames = (size_t)(duration * sample_rate);
    *out_samples = num_frames * 2; // Stereo
    
    *out_buffer = (int16_t *)malloc(*out_samples * sizeof(int16_t));
    if (!*out_buffer) return -1;
    
    double limit_dbfs = -0.5;
    double max_amp = pow(10.0, limit_dbfs / 20.0);
    
    double bpm = 120.0;
    double beat_len = 60.0 / bpm;
    
    for (size_t i = 0; i < num_frames; i++) {
        double t = (double)i / sample_rate;
        
        // 1. Bass / Kick (55Hz - 110Hz)
        double beat_phase = fmod(t, beat_len);
        double kick = 0.0;
        if (beat_phase < 0.2) {
            double freq = 110.0 - (110.0 - 55.0) * (beat_phase / 0.2);
            kick = sin(2.0 * M_PI * freq * beat_phase) * adsr(beat_phase, 0.2);
        }
        
        // 2. Lead Melody
        double chord_notes[] = {440.0, 523.25, 659.25, 523.25}; // A4, C5, E5, C5
        int note_idx = (int)(t / beat_len) % 4;
        double lead_freq = chord_notes[note_idx];
        double note_phase = fmod(t, beat_len);
        double lead = 0.0;
        // Sawtooth approximation with harmonics
        for (int h = 1; h <= 5; h++) {
            lead += sin(2.0 * M_PI * lead_freq * h * note_phase) / h;
        }
        lead *= adsr(note_phase, beat_len) * 0.3;
        
        // 3. Stereo Spatial Effect (L/R phase delay)
        double t_r = t - 0.015; // 15ms delay
        double lead_r = 0.0;
        if (t_r >= 0) {
            double note_phase_r = fmod(t_r, beat_len);
            for (int h = 1; h <= 5; h++) {
                lead_r += sin(2.0 * M_PI * lead_freq * h * note_phase_r) / h;
            }
            lead_r *= adsr(note_phase_r, beat_len) * 0.3;
        }
        
        double l = kick * 0.5 + lead;
        double r = kick * 0.5 + lead_r;
        
        // Limiter
        if (l > max_amp) l = max_amp;
        if (l < -max_amp) l = -max_amp;
        if (r > max_amp) r = max_amp;
        if (r < -max_amp) r = -max_amp;
        
        (*out_buffer)[i * 2] = (int16_t)(l * 32767.0);     // L
        (*out_buffer)[i * 2 + 1] = (int16_t)(r * 32767.0); // R
    }
    
    return 0;
}
