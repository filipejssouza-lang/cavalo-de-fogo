#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "governor_control.h"
#include "governor_daemon.h"

// Thresholds
#define TEMP_CPU_CRIT 65000 // 65C
#define TEMP_BAT_CRIT 350   // 35C

static int read_int_from_file(const char *path, int default_val) {
    FILE *f = fopen(path, "r");
    if (!f) return default_val;
    int val = default_val;
    if (fscanf(f, "%d", &val) != 1) { val = default_val; }
    fclose(f);
    return val;
}

static void write_status_file(const char *status) {
    int fd = open("/tmp/lupin_thermal_state", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, status, strlen(status));
        close(fd);
    }
}

static lupin_power_mode_t current_mode = LUPIN_POWER_UNKNOWN;
static time_t last_burst_time = 0;

void governor_daemon_loop(void) {
    lupin_power_idle();
    current_mode = LUPIN_POWER_IDLE;
    lupin_power_set_cooling(false);
    write_status_file("IDLE");

    const char *mock_cpu_temp = getenv("LUPIN_MOCK_CPU_TEMP_NODE");
    const char *mock_bat_temp = getenv("LUPIN_MOCK_BAT_TEMP_NODE");
    const char *cpu_node = mock_cpu_temp ? mock_cpu_temp : "/sys/class/thermal/thermal_zone0/temp";
    const char *bat_node = mock_bat_temp ? mock_bat_temp : "/sys/class/power_supply/battery/temp";

    while (1) {
        int cpu_temp = read_int_from_file(cpu_node, 40000);
        int bat_temp = read_int_from_file(bat_node, 300);

        bool overheating = (cpu_temp > TEMP_CPU_CRIT) || (bat_temp > TEMP_BAT_CRIT);

        // Forced Idle on overheating
        if (overheating) {
            if (current_mode != LUPIN_POWER_IDLE) {
                lupin_power_idle();
                current_mode = LUPIN_POWER_IDLE;
                write_status_file("IDLE (THERMAL THROTTLE)");
            }
            lupin_power_set_cooling(true); // Enable active cooling
        } else {
            lupin_power_set_cooling(false); // Disable cooling if under thresholds
            
            // For autonomous mode, default to BALANCED unless there is a burst requirement
            // To detect "load", we can read loadavg or just default to BALANCED when not overheating
            // In a real system, BURST is toggled by lupin-power explicitly, which sets mode.
            // Daemon should not overwrite BURST unless overheating or cooldown passed.
            
            // Check if another process requested burst via file (simple IPC for testing)
            FILE *f = fopen("/tmp/lupin_req_burst", "r");
            if (f) {
                fclose(f);
                unlink("/tmp/lupin_req_burst");
                lupin_power_burst();
                current_mode = LUPIN_POWER_BURST;
                last_burst_time = time(NULL);
                write_status_file("BURST");
            }
            
            // Cooldown logic: burst lasts 5 seconds if not renewed
            if (current_mode == LUPIN_POWER_BURST) {
                if (time(NULL) - last_burst_time > 5) {
                    lupin_power_balanced();
                    current_mode = LUPIN_POWER_BALANCED;
                    write_status_file("BALANCED");
                }
            } else if (current_mode == LUPIN_POWER_IDLE) {
                // If not overheating, return to balanced
                lupin_power_balanced();
                current_mode = LUPIN_POWER_BALANCED;
                write_status_file("BALANCED");
            }
        }
        
        sleep(2);
    }
}
