#define _GNU_SOURCE
#include "governor_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>

static void write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, val, strlen(val));
        close(fd);
    }
}

static void read_file_to_buf(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, size - 1);
        if (n >= 0) {
            buf[n] = '\0';
            // strip newline
            if (n > 0 && buf[n-1] == '\n') buf[n-1] = '\0';
        } else {
            buf[0] = '\0';
        }
        close(fd);
    } else {
        buf[0] = '\0';
    }
}

static void set_cpu_gov(int cpu, const char *gov) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
    write_file(path, gov);
}

static void set_cpu_freq(int cpu, const char *min_freq, const char *max_freq) {
    char path[256];
    if (min_freq) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
        write_file(path, min_freq);
    }
    if (max_freq) {
        snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
        write_file(path, max_freq);
    }
}

bool lupin_power_idle(void) {
    // Cores 0-3 (Cortex-A55) to conservative/powersave, min freq
    for (int i = 0; i <= 3; i++) {
        set_cpu_gov(i, "powersave");
        set_cpu_freq(i, "300000", "800000"); // typical low freq
    }
    
    // Cores 4-6 (Cortex-A78) and Core 7 (Cortex-X1) to powersave/deep sleep
    for (int i = 4; i <= 7; i++) {
        set_cpu_gov(i, "powersave");
        // We set max freq very low to force powersave
        set_cpu_freq(i, "300000", "300000"); 
    }
    
    return true;
}

bool lupin_power_balanced(void) {
    // Cores 0-3 (Cortex-A55) to schedutil/balanced
    for (int i = 0; i <= 3; i++) {
        set_cpu_gov(i, "schedutil");
        set_cpu_freq(i, "300000", "1804800"); 
    }
    
    // Cores 4-6 (Cortex-A78) to schedutil, mid freq
    for (int i = 4; i <= 6; i++) {
        set_cpu_gov(i, "schedutil");
        set_cpu_freq(i, "710400", "2419200"); 
    }
    
    // Core 7 (Cortex-X1) to schedutil, mid freq
    set_cpu_gov(7, "schedutil");
    set_cpu_freq(7, "844800", "2841600"); 
    
    return true;
}

void lupin_power_set_cooling(bool active) {
    // Write state file for telemetry and HUD
    write_file("/tmp/lupin_peltier_state", active ? "ACTIVE\n" : "STANDBY\n");

    const char *mock_path = getenv("LUPIN_MOCK_COOLING_NODE");
    if (mock_path) {
        write_file(mock_path, active ? "1" : "0");
        return;
    }

    // Scan /sys/class/thermal/ for cooling devices (fan, peltier, pwm)
    bool found = false;
    for (int i = 0; i < 16; i++) {
        char type_path[256];
        char cur_path[256];
        char max_path[256];
        char type_buf[64] = {0};
        char max_buf[16] = "1";

        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/cooling_device%d/type", i);
        snprintf(cur_path, sizeof(cur_path), "/sys/class/thermal/cooling_device%d/cur_state", i);
        snprintf(max_path, sizeof(max_path), "/sys/class/thermal/cooling_device%d/max_state", i);

        if (access(type_path, R_OK) == 0) {
            read_file_to_buf(type_path, type_buf, sizeof(type_buf));
            read_file_to_buf(max_path, max_buf, sizeof(max_buf));
            if (strstr(type_buf, "fan") || strstr(type_buf, "peltier") || strstr(type_buf, "pwm") || strstr(type_buf, "cooler")) {
                write_file(cur_path, active ? max_buf : "0");
                found = true;
            }
        }
    }

    if (!found) {
        // Fallback to cooling_device0
        write_file("/sys/class/thermal/cooling_device0/cur_state", active ? "1" : "0");
    }
}

void lupin_power_set_peltier_mode(lupin_peltier_mode_t mode) {
    switch (mode) {
        case PELTIER_MODE_ON:
            write_file("/tmp/lupin_peltier_override", "ON\n");
            lupin_power_set_cooling(true);
            break;
        case PELTIER_MODE_OFF:
            write_file("/tmp/lupin_peltier_override", "OFF\n");
            lupin_power_set_cooling(false);
            break;
        case PELTIER_MODE_AUTO:
        default:
            unlink("/tmp/lupin_peltier_override");
            break;
    }
}

bool lupin_power_get_peltier_state(void) {
    char buf[32] = {0};
    read_file_to_buf("/tmp/lupin_peltier_state", buf, sizeof(buf));
    return (strstr(buf, "ACTIVE") != NULL);
}

bool lupin_power_burst(void) {
    // Leave Cores 0-6 alone or keep them somewhat low
    
    // Core 7 (Cortex-X1) to performance, max freq
    set_cpu_gov(7, "performance");
    set_cpu_freq(7, "2841600", "2841600"); // typical snapdragon 888 X1 max freq
    
    // Bind current process to Core 7
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(7, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        return false;
    }
    
    return true;
}

void lupin_power_status(void) {
    printf("[Lupin Power Status]\n");
    for (int i = 0; i <= 7; i++) {
        char path_gov[256];
        char path_freq[256];
        char gov[64] = "unknown";
        char freq[64] = "unknown";
        
        snprintf(path_gov, sizeof(path_gov), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        snprintf(path_freq, sizeof(path_freq), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        
        read_file_to_buf(path_gov, gov, sizeof(gov));
        read_file_to_buf(path_freq, freq, sizeof(freq));
        
        printf("Core %d: Governor=%-12s Freq=%s kHz\n", i, gov, freq);
    }
}
