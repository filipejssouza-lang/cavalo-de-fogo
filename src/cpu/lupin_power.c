#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>
#include "governor_control.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <idle|balanced|burst <cmd>|status|thermal status|peltier <on|off|auto|status>>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "peltier") == 0) {
        if (argc < 3 || strcmp(argv[2], "status") == 0) {
            bool active = lupin_power_get_peltier_state();
            printf("Peltier Cooler State: %s\n", active ? "ACTIVE (COOLING ON)" : "STANDBY (COOLING OFF)");
            return 0;
        }
        if (strcmp(argv[2], "on") == 0) {
            lupin_power_set_peltier_mode(PELTIER_MODE_ON);
            printf("Peltier Cooler forced ON.\n");
        } else if (strcmp(argv[2], "off") == 0) {
            lupin_power_set_peltier_mode(PELTIER_MODE_OFF);
            printf("Peltier Cooler forced OFF.\n");
        } else if (strcmp(argv[2], "auto") == 0) {
            lupin_power_set_peltier_mode(PELTIER_MODE_AUTO);
            printf("Peltier Cooler set to AUTO (Closed-Loop Governor).\n");
        } else {
            printf("Usage: %s peltier <on|off|auto|status>\n", argv[0]);
            return 1;
        }
        return 0;
    }

    if (strcmp(argv[1], "thermal") == 0 && argc == 3 && strcmp(argv[2], "status") == 0) {
        FILE *f = fopen("/tmp/lupin_thermal_state", "r");
        if (f) {
            char state[64] = {0};
            if (fgets(state, sizeof(state), f)) {
                printf("Thermal State: %s\n", state);
            }
            fclose(f);
        } else {
            printf("Thermal State: UNKNOWN\n");
        }
        return 0;
    }

    if (strcmp(argv[1], "idle") == 0) {
        if (lupin_power_idle()) {
            printf("CPU set to IDLE mode.\n");
        } else {
            printf("Failed to set CPU to IDLE mode.\n");
        }
    } else if (strcmp(argv[1], "balanced") == 0) {
        if (lupin_power_balanced()) {
            printf("CPU set to BALANCED mode.\n");
        } else {
            printf("Failed to set CPU to BALANCED mode.\n");
        }
    } else if (strcmp(argv[1], "burst") == 0) {
        if (argc < 3) {
            printf("Usage for burst: %s burst <command> [args...]\n", argv[0]);
            return 1;
        }

        if (lupin_power_burst()) {
            printf("CPU set to BURST mode.\n");
            // Signal governor daemon
            FILE *f = fopen("/tmp/lupin_req_burst", "w");
            if (f) {
                fprintf(f, "1");
                fclose(f);
            }
        } else {
            printf("Failed to set CPU to BURST mode.\n");
            return 1;
        }

        // Bind current process to Core 7
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(7, &cpuset);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
            perror("sched_setaffinity");
        } else {
            printf("Process bound to Core 7 (Cortex-X1).\n");
        }

        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[2], &argv[2]);
            perror("execvp");
            exit(1);
        } else if (pid > 0) {
            waitpid(pid, NULL, 0);
            printf("Computation finished. Returning to IDLE mode.\n");
            lupin_power_idle();
            // We do not need to signal daemon to go back, it has 5s timeout or will adjust automatically,
            // but we can signal it's done by writing another IPC or letting it timeout.
        } else {
            perror("fork");
            lupin_power_idle();
            return 1;
        }
    } else if (strcmp(argv[1], "status") == 0) {
        lupin_power_status();
    } else {
        printf("Unknown command: %s\n", argv[1]);
        printf("Usage: %s <idle|balanced|burst <cmd>|status|thermal status>\n", argv[0]);
        return 1;
    }

    return 0;
}
