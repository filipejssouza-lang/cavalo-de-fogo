#include "lupin_audio.h"
#include "lupin_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_usage(void) {
    printf("Lupin OS v6 Bare-Metal Audio Subsystem\n");
    printf("Usage:\n");
    printf("  lupin-audio beep [freq_hz] [duration_ms]\n");
    printf("  lupin-audio play <filepath.wav>\n");
    printf("  lupin-audio music\n");
    printf("  lupin-audio vol <0-100>\n");
    printf("  lupin-audio status\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "beep") == 0) {
        float freq = 440.0f;
        int duration = 300;
        if (argc >= 3) freq = atof(argv[2]);
        if (argc >= 4) duration = atoi(argv[3]);

        int16_t *buf = NULL;
        size_t samples = 0;
        int sample_rate = 48000;
        if (lupin_audio_generate_tone(freq, duration, sample_rate, &buf, &samples) == 0) {
            int ret = lupin_audio_play_pcm(buf, samples * sizeof(int16_t), sample_rate, 1, 16);
            free(buf);
            return ret;
        }
        return 1;
    } else if (strcmp(argv[1], "play") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Missing filepath.\n");
            return 1;
        }
        return lupin_music_play_file(argv[2]);
    } else if (strcmp(argv[1], "music") == 0) {
        int16_t *buf = NULL;
        size_t samples = 0;
        int sample_rate = 48000;
        if (lupin_music_generate_theme(sample_rate, &buf, &samples) == 0) {
            // out_samples holds number of stereo samples (2 * frames), size in bytes is * sizeof(int16_t)
            int ret = lupin_audio_play_pcm(buf, samples * sizeof(int16_t), sample_rate, 2, 16);
            free(buf);
            return ret;
        }
        return 1;
    } else if (strcmp(argv[1], "vol") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Missing volume (0-100).\n");
            return 1;
        }
        int vol = atoi(argv[2]);
        return lupin_audio_set_volume(vol);
    } else if (strcmp(argv[1], "status") == 0) {
        lupin_audio_status_t st;
        if (lupin_audio_get_status(&st) == 0) {
            printf("{\"volume\": %d, \"muted\": %d, \"device\": \"%s\", \"mock\": %s}\n", 
                   st.volume, st.is_muted, st.pcm_device, st.is_mock ? "true" : "false");
            return 0;
        }
        return 1;
    }

    print_usage();
    return 1;
}
