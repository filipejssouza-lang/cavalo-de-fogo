#include "lupin_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_generate_theme() {
    int16_t *buf = NULL;
    size_t samples = 0;
    int sample_rate = 48000;
    int ret = lupin_music_generate_theme(sample_rate, &buf, &samples);
    assert(ret == 0);
    assert(buf != NULL);
    
    // Duration is 12 seconds, stereo
    assert(samples == (size_t)(12.0 * sample_rate * 2));
    
    // Check clipping
    for (size_t i = 0; i < samples; i++) {
        // -0.5 dBFS limit is about 0.944, which is roughly 30935 in 16-bit
        assert(buf[i] <= 30950);
        assert(buf[i] >= -30950);
    }
    
    free(buf);
    printf("[PASS] test_generate_theme\n");
}

void test_play_wav_mock() {
    unlink(LUPIN_MOCK_PCM_FILE);
    
    // Create a dummy WAV file
    const char *wav_path = "/tmp/test_music_sample.wav";
    FILE *f = fopen(wav_path, "wb");
    assert(f != NULL);

    char riff[4] = "RIFF";
    uint32_t rsize = 36;
    char wave[4] = "WAVE";
    char fmt[4] = "fmt ";
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 2;
    uint32_t sample_rate = 48000;
    uint32_t byte_rate = 48000 * 4;
    uint16_t block_align = 4;
    uint16_t bits_per_sample = 16;
    char data[4] = "data";
    uint32_t data_size = 32;

    fwrite(riff, 1, 4, f);
    fwrite(&rsize, 1, 4, f);
    fwrite(wave, 1, 4, f);
    fwrite(fmt, 1, 4, f);
    fwrite(&fmt_size, 1, 4, f);
    fwrite(&audio_format, 1, 2, f);
    fwrite(&num_channels, 1, 2, f);
    fwrite(&sample_rate, 1, 4, f);
    fwrite(&byte_rate, 1, 4, f);
    fwrite(&block_align, 1, 2, f);
    fwrite(&bits_per_sample, 1, 2, f);
    fwrite(data, 1, 4, f);
    fwrite(&data_size, 1, 4, f);

    int16_t samples[16] = {0};
    fwrite(samples, sizeof(int16_t), 16, f);
    fclose(f);

    setenv("LUPIN_PCM_DEVICE", "/dev/snd/does_not_exist_123", 1);
    
    int ret = lupin_music_play_file(wav_path);
    assert(ret == 0);
    
    FILE *mf = fopen(LUPIN_MOCK_PCM_FILE, "rb");
    assert(mf != NULL);
    fseek(mf, 0, SEEK_END);
    long sz = ftell(mf);
    assert(sz == 32);
    fclose(mf);
    
    unlink(wav_path);
    printf("[PASS] test_play_wav_mock\n");
}

int main() {
    printf("Running Music Tests...\n");
    test_generate_theme();
    test_play_wav_mock();
    printf("All Music Tests Passed!\n");
    return 0;
}
