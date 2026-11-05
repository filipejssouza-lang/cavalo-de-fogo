#ifndef NET_WATCHDOG_H
#define NET_WATCHDOG_H

// Infinite loop to monitor network interfaces (eth0, rndis0/usb0, wlan0)
void net_watchdog_loop(void);

#endif // NET_WATCHDOG_H
