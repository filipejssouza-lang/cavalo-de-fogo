#ifndef LUPIN_MUSIC_H
#define LUPIN_MUSIC_H

#include <stdint.h>
#include <stddef.h>

#define LUPIN_DEFAULT_PCM_DEVICE "/dev/snd/pcmC0D0p"
#define LUPIN_DEFAULT_MIXER_DEVICE "/dev/snd/controlC0"
#define LUPIN_MOCK_PCM_FILE "/tmp/lupin_music_mock.pcm"
#define LUPIN_MOCK_VOL_FILE "/tmp/lupin_audio_mock_vol"

// Synthesizes a 12-15s stereo polyphonic theme song
int lupin_music_generate_theme(int sample_rate, int16_t **out_buffer, size_t *out_samples);

// Plays a WAV file using chunks, supporting stereo
int lupin_music_play_file(const char *filepath);

#endif // LUPIN_MUSIC_H
