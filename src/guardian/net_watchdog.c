#include "net_watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static void log_kmsg(const char *msg) {
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

static int interface_exists(const char *iface) {
    char path[256];
    snprintf(path, sizeof(path), "/sys/class/net/%s", iface);
    struct stat st;
    return stat(path, &st) == 0;
}

static int interface_is_up(const char *iface) {
    char path[256];
    char buf[16];
    snprintf(path, sizeof(path), "/sys/class/net/%s/operstate", iface);
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            if (strstr(buf, "up") != NULL) {
                return 1;
            }
        }
    }
    return 0;
}

void net_watchdog_loop(void) {
    int eth0_configured = 0;
    int usb_configured = 0;
    int wlan0_logged = 0;

    log_kmsg("[LupinNetWatchdog] Inciando monitoramento de interfaces...\n");

    while (1) {
        // Check eth0 (OTG RJ45)
        if (interface_exists("eth0") && !eth0_configured) {
            log_kmsg("[LupinNetWatchdog] eth0 detectado! Configurando 192.168.31.101/24...\n");
            system("ifconfig eth0 up");
            system("ifconfig eth0 192.168.31.101 netmask 255.255.255.0");
            system("route add default gw 192.168.31.1 eth0");
            system("echo 'nameserver 8.8.8.8' > /etc/resolv.conf");
            eth0_configured = 1;
        } else if (!interface_exists("eth0")) {
            eth0_configured = 0;
        }

        // Check usb0/rndis0 (USB Tethering)
        const char *usb_iface = NULL;
        if (interface_exists("usb0")) {
            usb_iface = "usb0";
        } else if (interface_exists("rndis0")) {
            usb_iface = "rndis0";
        }

        if (usb_iface && !usb_configured) {
            char log_buf[256];
            snprintf(log_buf, sizeof(log_buf), "[LupinNetWatchdog] %s detectado! Configurando 192.168.1.10/24...\n", usb_iface);
            log_kmsg(log_buf);

            char cmd[256];
            snprintf(cmd, sizeof(cmd), "ifconfig %s up", usb_iface);
            system(cmd);

            snprintf(cmd, sizeof(cmd), "ifconfig %s 192.168.1.10 netmask 255.255.255.0", usb_iface);
            system(cmd);

            usb_configured = 1;
        } else if (!usb_iface) {
            usb_configured = 0;
        }

        // Check wlan0
        if (interface_exists("wlan0") && interface_is_up("wlan0") && !wlan0_logged) {
            log_kmsg("[LupinNetWatchdog] wlan0 ativa. Gerenciamento DHCP assumido pelo SO.\n");
            wlan0_logged = 1;
        } else if (!interface_is_up("wlan0")) {
            wlan0_logged = 0;
        }

        sleep(3);
    }
}
