#ifndef GOVERNOR_CONTROL_H
#define GOVERNOR_CONTROL_H

#include <stdbool.h>

typedef enum {
    LUPIN_POWER_UNKNOWN = 0,
    LUPIN_POWER_IDLE,
    LUPIN_POWER_BALANCED,
    LUPIN_POWER_BURST
} lupin_power_mode_t;

// Set the CPU to idle mode (Cores 0-3 at min freq, Cores 4-7 in powersave)
bool lupin_power_idle(void);

// Set the CPU to balanced mode (Cores 0-3 balanced, Cores 4-7 mid freq)
bool lupin_power_balanced(void);

// Toggle active cooling (Peltier/Fan)
void lupin_power_set_cooling(bool active);

// Set the CPU to burst mode (Core 7 at max freq and bound to the calling thread)
bool lupin_power_burst(void);

// Get the current power mode status and print it
void lupin_power_status(void);

#endif // GOVERNOR_CONTROL_H
