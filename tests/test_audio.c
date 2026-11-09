#include "lupin_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_generate_tone() {
    int16_t *buf = NULL;
    size_t samples = 0;
    int ret = lupin_audio_generate_tone(440.0f, 300, 48000, &buf, &samples);
    assert(ret == 0);
    assert(buf != NULL);
    assert(samples == (size_t)(0.300 * 48000));
    
    // Check fade in (starts at 0 and grows)
    assert(buf[0] == 0);
    
    // Cleanup
    free(buf);
    printf("[PASS] test_generate_tone\n");
}

void test_alsa_fallback_mock() {
    unlink(LUPIN_MOCK_PCM_FILE);
    int16_t data[10] = {0};
    // Ensure LUPIN_PCM_DEVICE points to something non-existent
    setenv("LUPIN_PCM_DEVICE", "/dev/snd/does_not_exist_123", 1);
    
    int ret = lupin_audio_play_pcm(data, sizeof(data), 48000, 1, 16);
    assert(ret == 0); // Mock fallback should succeed
    
    FILE *f = fopen(LUPIN_MOCK_PCM_FILE, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    assert(sz == sizeof(data));
    fclose(f);
    
    printf("[PASS] test_alsa_fallback_mock\n");
}

void test_volume_mock() {
    unlink(LUPIN_MOCK_VOL_FILE);
    setenv("LUPIN_MIXER_DEVICE", "/dev/snd/does_not_exist_mixer", 1);
    int ret = lupin_audio_set_volume(75);
    assert(ret == 0);
    
    int vol = lupin_audio_get_volume();
    assert(vol == 75);
    
    printf("[PASS] test_volume_mock\n");
}

void test_wav_parser() {
    const char *wav_path = "/tmp/test_audio_sample.wav";
    FILE *f = fopen(wav_path, "wb");
    assert(f != NULL);

    lupin_wav_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.chunk_id, "RIFF", 4);
    hdr.chunk_size = sizeof(hdr) - 8 + 32;
    memcpy(hdr.format, "WAVE", 4);
    memcpy(hdr.subchunk1_id, "fmt ", 4);
    hdr.subchunk1_size = 16;
    hdr.audio_format = 1; // PCM
    hdr.num_channels = 1;
    hdr.sample_rate = 48000;
    hdr.byte_rate = 48000 * 2;
    hdr.block_align = 2;
    hdr.bits_per_sample = 16;
    memcpy(hdr.subchunk2_id, "data", 4);
    hdr.subchunk2_size = 32;

    fwrite(&hdr, 1, sizeof(hdr), f);
    int16_t samples[16] = {0};
    fwrite(samples, sizeof(int16_t), 16, f);
    fclose(f);

    setenv("LUPIN_PCM_DEVICE", "/dev/snd/does_not_exist_123", 1);
    int ret = lupin_audio_play_wav(wav_path);
    assert(ret == 0);
    unlink(wav_path);
    printf("[PASS] test_wav_parser\n");
}

int main() {
    printf("Running Audio Tests...\n");
    test_generate_tone();
    test_alsa_fallback_mock();
    test_volume_mock();
    test_wav_parser();
    printf("All Audio Tests Passed!\n");
    return 0;
}
