#ifndef LUPIN_AUDIO_H
#define LUPIN_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#define LUPIN_DEFAULT_PCM_DEVICE "/dev/snd/pcmC0D0p"
#define LUPIN_DEFAULT_MIXER_DEVICE "/dev/snd/controlC0"
#define LUPIN_MOCK_PCM_FILE "/tmp/lupin_audio_mock.pcm"
#define LUPIN_MOCK_VOL_FILE "/tmp/lupin_audio_mock_vol"

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
    char subchunk2_id[4];
    uint32_t subchunk2_size;
} __attribute__((packed)) lupin_wav_header_t;

// Audio Subsystem Status
typedef struct {
    int volume; // 0-100
    int is_muted;
    char pcm_device[128];
    int is_mock;
} lupin_audio_status_t;

// Core functions
int lupin_audio_generate_tone(float freq_hz, int duration_ms, int sample_rate, int16_t **out_buffer, size_t *out_samples);
int lupin_audio_play_pcm(const void *pcm_data, size_t data_size, int sample_rate, int channels, int bits_per_sample);
int lupin_audio_play_wav(const char *filepath);

// Mixer & Volume
int lupin_audio_set_volume(int volume_percent); // 0-100
int lupin_audio_get_volume(void);
int lupin_audio_set_mute(int mute); // 1 = mute, 0 = unmute

// Status
int lupin_audio_get_status(lupin_audio_status_t *status);

#endif // LUPIN_AUDIO_H
