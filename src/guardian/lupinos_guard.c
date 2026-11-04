#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <dirent.h>

#define ZRAM_DISKSIZE_BYTES 12884901888ULL // 12GB
#define ZRAM_DISKSIZE_STR "12884901888"

// Linux swap definitions (from linux/swap.h)
#ifndef SWAP_FLAG_PREFER
#define SWAP_FLAG_PREFER    0x8000
#endif
#ifndef SWAP_FLAG_PRIO_MASK
#define SWAP_FLAG_PRIO_MASK 0x7fff
#endif

static void write_file(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
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
        } else {
            buf[0] = '\0';
        }
        close(fd);
    } else {
        buf[0] = '\0';
    }
}

static void log_kmsg(const char *msg) {
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

// 1. Display Power Management (Keep AMOLED off during headless AI / server runs)
static void screen_off() {
    write_file("/sys/class/backlight/panel0-backlight/brightness", "0");
}

// 2. ZRAM Provisioning (12.0 GB Swap @ Priority 100)
static void setup_zram() {
    log_kmsg("[LupinGuard] Configurando 12.0 GB ZRAM Swap...\n");
    
    // Reset existing ZRAM
    write_file("/sys/block/zram0/reset", "1");
    write_file("/sys/block/zram0/disksize", ZRAM_DISKSIZE_STR);

    // Try mkswap using available system utilities first
    int status = -1;
    pid_t pid = fork();
    if (pid == 0) {
        execl("/system/bin/mkswap", "mkswap", "/dev/block/zram0", NULL);
        execl("/bin/mkswap", "mkswap", "/dev/block/zram0", NULL);
        execl("/sbin/mkswap", "mkswap", "/dev/block/zram0", NULL);
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
    }

    // Direct header fallback if external mkswap was not found
    if (status != 0) {
        int fd = open("/dev/block/zram0", O_RDWR);
        if (fd >= 0) {
            int page_size = sysconf(_SC_PAGESIZE);
            if (page_size <= 0) page_size = 4096;
            
            char *page = (char *)aligned_alloc(page_size, page_size);
            if (page) {
                memset(page, 0, page_size);
                // Swap header v1
                unsigned int *hdr = (unsigned int *)(page + 1024);
                hdr[0] = 1; // version 1
                hdr[1] = (unsigned int)((ZRAM_DISKSIZE_BYTES / page_size) - 1); // last page
                hdr[2] = 0; // bad pages count
                
                // Signature at offset (page_size - 10)
                memcpy(page + page_size - 10, "SWAPSPACE2", 10);
                
                pwrite(fd, page, page_size, 0);
                free(page);
            }
            close(fd);
        }
    }

    // swapon syscall with priority 100
    // SWAP_FLAG_PREFER (0x8000) | (priority & 0x7fff)
    int swap_flags = SWAP_FLAG_PREFER | (100 & SWAP_FLAG_PRIO_MASK);
    long ret = syscall(SYS_swapon, "/dev/block/zram0", swap_flags);
    if (ret == 0) {
        log_kmsg("[LupinGuard] ZRAM 12.0 GB Swap ativado com sucesso!\n");
    } else {
        log_kmsg("[LupinGuard] AVISO: swapon falhou na syscall direta, tentando comando swapon...\n");
        pid_t spid = fork();
        if (spid == 0) {
            execl("/system/bin/swapon", "swapon", "-p", "100", "/dev/block/zram0", NULL);
            execl("/bin/swapon", "swapon", "-p", "100", "/dev/block/zram0", NULL);
            exit(1);
        } else if (spid > 0) {
            waitpid(spid, NULL, 0);
        }
    }
}

// 3. USB & Power Lock (Prevent deep sleep on dwc3 and keep cluster alive)
static void usb_power_lock() {
    write_file("/sys/module/usbcore/parameters/autosuspend", "-1");
    write_file("/sys/power/wake_lock", "lupinos_usb");
}

// 4. Resilient /data Mount Sentinel
static int is_data_mounted() {
    char buf[4096];
    read_file_to_buf("/proc/mounts", buf, sizeof(buf));
    return (strstr(buf, " /data ") != NULL);
}

static void sentinel_loop() {
    int attempts = 0;
    while (attempts < 60) {
        struct stat st;
        if (stat("/data/start_chroot_auto.sh", &st) == 0) {
            log_kmsg("[LupinGuard] /data/start_chroot_auto.sh detectado! Acionando autostart...\n");
            pid_t pid = fork();
            if (pid == 0) {
                execl("/system/bin/sh", "sh", "/data/start_chroot_auto.sh", NULL);
                execl("/bin/sh", "sh", "/data/start_chroot_auto.sh", NULL);
                execl("/sbin/sh", "sh", "/data/start_chroot_auto.sh", NULL);
                exit(1);
            }
            break;
        }

        // If not mounted after 5 seconds, proactively try mounting
        if (attempts >= 5 && !is_data_mounted()) {
            mount("/dev/block/bootdevice/by-name/userdata", "/data", "f2fs", MS_NOATIME, "discard");
            if (!is_data_mounted()) {
                mount("/dev/block/sda34", "/data", "f2fs", MS_NOATIME, "discard");
            }
        }

        sleep(1);
        attempts++;
    }
}

// 5. Thermal Watchdog (Target: keep Peltier-cooled SoC under 65C)
static void thermal_loop() {
    while (1) {
        DIR *dir = opendir("/sys/class/thermal");
        if (dir) {
            struct dirent *ent;
            while ((ent = readdir(dir)) != NULL) {
                if (strncmp(ent->d_name, "thermal_zone", 12) == 0) {
                    char path[256];
                    snprintf(path, sizeof(path), "/sys/class/thermal/%s/temp", ent->d_name);
                    char buf[32] = {0};
                    read_file_to_buf(path, buf, sizeof(buf));
                    int temp = atoi(buf);
                    if (temp > 65000 || (temp > 65 && temp < 1000)) {
                        char warn[128];
                        snprintf(warn, sizeof(warn), "[LupinGuard] ALERTA: %s atingiu %d mC!\n", ent->d_name, temp);
                        log_kmsg(warn);
                    }
                }
            }
            closedir(dir);
        }
        sleep(10);
    }
}

int main(void) {
    log_kmsg("[LupinGuard] Lupin Bare-Metal OS v3 Guardian Iniciado!\n");

    screen_off();
    setup_zram();
    usb_power_lock();

    pid_t sentinel_pid = fork();
    if (sentinel_pid == 0) {
        sentinel_loop();
        exit(0);
    }

    pid_t thermal_pid = fork();
    if (thermal_pid == 0) {
        thermal_loop();
        exit(0);
    }

    // Main process remains supervising
    while (1) {
        pause();
    }
    return 0;
}
