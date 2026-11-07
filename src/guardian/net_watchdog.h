#ifndef NET_WATCHDOG_H
#define NET_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

#define NET_PING_FAIL_LIMIT 3
#define NET_PING_RESTORE_LIMIT 5

typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_PROBING_UP,
    NET_STATE_UP,
    NET_STATE_PROBING_DOWN
} net_link_state_t;

// ICMP raw socket ping probe with SO_RCVTIMEO
bool net_probe_icmp(const char *iface, const char *ip_target, int timeout_ms);

// Anti-flap state machine step
net_link_state_t net_watchdog_step_state(
    bool probe_ok,
    net_link_state_t current_state,
    int *consec_success,
    int *consec_failure,
    bool *state_changed
);

// Infinite loop to monitor network interfaces (eth0, rndis0/usb0, wlan0)
void net_watchdog_loop(void);

#endif // NET_WATCHDOG_H
