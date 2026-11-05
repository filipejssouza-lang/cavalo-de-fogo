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
        printf("Usage: %s <idle|burst <cmd>|status>\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "idle") == 0) {
        if (lupin_power_idle()) {
            printf("CPU set to IDLE mode.\n");
        } else {
            printf("Failed to set CPU to IDLE mode.\n");
        }
    } else if (strcmp(argv[1], "burst") == 0) {
        if (argc < 3) {
            printf("Usage for burst: %s burst <command> [args...]\n", argv[0]);
            return 1;
        }

        if (lupin_power_burst()) {
            printf("CPU set to BURST mode.\n");
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
        } else {
            perror("fork");
            lupin_power_idle();
            return 1;
        }
    } else if (strcmp(argv[1], "status") == 0) {
        lupin_power_status();
    } else {
        printf("Unknown command: %s\n", argv[1]);
        printf("Usage: %s <idle|burst <cmd>|status>\n", argv[0]);
        return 1;
    }

    return 0;
}
