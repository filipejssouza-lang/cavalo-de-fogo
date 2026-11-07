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

// Peltier Cooler Modes
typedef enum {
    PELTIER_MODE_AUTO = 0,
    PELTIER_MODE_ON,
    PELTIER_MODE_OFF
} lupin_peltier_mode_t;

// Set Peltier mode (auto, on, off)
void lupin_power_set_peltier_mode(lupin_peltier_mode_t mode);

// Get current Peltier state (returns true if currently active)
bool lupin_power_get_peltier_state(void);

// Set the CPU to burst mode (Core 7 at max freq and bound to the calling thread)
bool lupin_power_burst(void);

// Get the current power mode status and print it
void lupin_power_status(void);

#endif // GOVERNOR_CONTROL_H
