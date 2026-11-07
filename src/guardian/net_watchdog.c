#define _GNU_SOURCE
#include "net_watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <errno.h>

#define ETH0_GW_IP "192.168.31.1"
#define ETH0_LOCAL_IP "192.168.31.101"
#define CLOUD_DNS_IP "1.1.1.1"

static void log_kmsg(const char *msg) {
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, strlen(msg));
        close(fd);
    }
}

static void write_atomic_file(const char *target_path, const char *content) {
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.lupin_tmp", target_path);

    int fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, content, strlen(content));
        close(fd);
        rename(tmp_path, target_path);
    }
}

static uint16_t icmp_checksum(void *b, int len) {
    uint16_t *buf = b;
    uint32_t sum = 0;
    for (sum = 0; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(uint8_t *)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
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

bool net_probe_icmp(const char *iface, const char *ip_target, int timeout_ms) {
    const char *mock_env = getenv("LUPIN_MOCK_NET_PING");
    if (mock_env) {
        return (strcmp(mock_env, "1") == 0 || strcmp(mock_env, "true") == 0);
    }

    if (iface && !interface_exists(iface)) {
        return false;
    }

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) {
        // Fallback for non-root / containers lacking CAP_NET_RAW
        if (errno == EPERM || errno == EACCES) {
            if (iface) {
                return (interface_exists(iface) && interface_is_up(iface));
            }
            return true;
        }
        return false;
    }

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#ifdef SO_BINDTODEVICE
    if (iface && strlen(iface) > 0) {
        setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface));
    }
#endif

    static uint16_t seq = 0;
    struct icmphdr icmp;
    memset(&icmp, 0, sizeof(icmp));
    icmp.type = ICMP_ECHO;
    icmp.code = 0;
    icmp.un.echo.id = htons((uint16_t)(getpid() & 0xFFFF));
    icmp.un.echo.sequence = htons(seq++);
    icmp.checksum = icmp_checksum(&icmp, sizeof(icmp));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    if (inet_pton(AF_INET, ip_target, &dst.sin_addr) <= 0) {
        close(fd);
        return false;
    }

    if (sendto(fd, &icmp, sizeof(icmp), 0, (struct sockaddr *)&dst, sizeof(dst)) <= 0) {
        close(fd);
        return false;
    }

    char buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
    close(fd);

    if (n >= (ssize_t)(sizeof(struct iphdr) + sizeof(struct icmphdr))) {
        struct iphdr *ip = (struct iphdr *)buf;
        int ip_hdr_len = ip->ihl * 4;
        if (n >= ip_hdr_len + (int)sizeof(struct icmphdr)) {
            struct icmphdr *reply = (struct icmphdr *)(buf + ip_hdr_len);
            if (reply->type == ICMP_ECHOREPLY && reply->un.echo.id == icmp.un.echo.id) {
                return true;
            }
        }
    }

    return false;
}

net_link_state_t net_watchdog_step_state(
    bool probe_ok,
    net_link_state_t current_state,
    int *consec_success,
    int *consec_failure,
    bool *state_changed
) {
    if (state_changed) *state_changed = false;

    if (current_state == NET_STATE_UP || current_state == NET_STATE_PROBING_DOWN) {
        if (probe_ok) {
            *consec_failure = 0;
            return NET_STATE_UP;
        } else {
            (*consec_failure)++;
            if (*consec_failure >= NET_PING_FAIL_LIMIT) {
                if (state_changed) *state_changed = true;
                *consec_failure = 0;
                *consec_success = 0;
                return NET_STATE_DOWN;
            }
            return NET_STATE_PROBING_DOWN;
        }
    } else { // NET_STATE_DOWN or NET_STATE_PROBING_UP
        if (probe_ok) {
            (*consec_success)++;
            if (*consec_success >= NET_PING_RESTORE_LIMIT) {
                if (state_changed) *state_changed = true;
                *consec_success = 0;
                *consec_failure = 0;
                return NET_STATE_UP;
            }
            return NET_STATE_PROBING_UP;
        } else {
            *consec_success = 0;
            return NET_STATE_DOWN;
        }
    }
}

static void apply_eth0_up(void) {
    log_kmsg("[LupinNetWatchdog] eth0 RESTAURADO (5 pings estaveis). Promovendo para rota padrao...\n");
    system("ifconfig eth0 up");
    system("ifconfig eth0 " ETH0_LOCAL_IP " netmask 255.255.255.0");
    system("route add default gw " ETH0_GW_IP " metric 10 eth0 2>/dev/null || route replace default gw " ETH0_GW_IP " eth0 2>/dev/null");

    const char *resolv_conf = "nameserver 1.1.1.1\nnameserver 8.8.8.8\n";
    if (access("/etc", W_OK) == 0) {
        write_atomic_file("/etc/resolv.conf", resolv_conf);
    }
    write_atomic_file("/tmp/lupin_resolv.conf", resolv_conf);
    write_atomic_file("/tmp/lupin_net_state", "INTERFACE=eth0\nSTATUS=ONLINE\nIP=" ETH0_LOCAL_IP "\nGATEWAY=" ETH0_GW_IP "\n");
}

static void apply_eth0_down(void) {
    log_kmsg("[LupinNetWatchdog] eth0 CAIU (3 falhas consecutivas). Executando failover defensivo...\n");
    system("route del default gw " ETH0_GW_IP " dev eth0 2>/dev/null || true");
    if (interface_exists("wlan0") && interface_is_up("wlan0")) {
        log_kmsg("[LupinNetWatchdog] Failover: wlan0 promovido a link primario de sobrevivencia.\n");
        write_atomic_file("/tmp/lupin_net_state", "INTERFACE=wlan0\nSTATUS=FAILOVER\n");
    } else {
        log_kmsg("[LupinNetWatchdog] ALERTA: Nenhuma interface alternativa disponivel!\n");
        write_atomic_file("/tmp/lupin_net_state", "INTERFACE=NONE\nSTATUS=ISOLATED\n");
    }
}

void net_watchdog_loop(void) {
    net_link_state_t eth0_state = NET_STATE_DOWN;
    int eth0_consec_success = 0;
    int eth0_consec_failure = 0;
    int usb_configured = 0;
    int wlan0_logged = 0;

    log_kmsg("[LupinNetWatchdog] Sonda ICMP Nativa + Histerese Anti-Flap Ativada.\n");

    while (1) {
        if (interface_exists("eth0")) {
            bool ping_ok = net_probe_icmp("eth0", ETH0_GW_IP, 1000);
            if (!ping_ok) {
                // Secondary probe to public DNS to differentiate gateway drop from WAN drop
                ping_ok = net_probe_icmp("eth0", CLOUD_DNS_IP, 1000);
            }

            bool state_changed = false;
            net_link_state_t next_state = net_watchdog_step_state(
                ping_ok,
                eth0_state,
                &eth0_consec_success,
                &eth0_consec_failure,
                &state_changed
            );

            if (state_changed) {
                if (next_state == NET_STATE_UP) {
                    apply_eth0_up();
                } else if (next_state == NET_STATE_DOWN) {
                    apply_eth0_down();
                }
            }
            eth0_state = next_state;
        } else {
            if (eth0_state != NET_STATE_DOWN) {
                apply_eth0_down();
                eth0_state = NET_STATE_DOWN;
                eth0_consec_success = 0;
                eth0_consec_failure = 0;
            }
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

        sleep(1);
    }
}
