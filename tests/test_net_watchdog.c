#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "../src/guardian/net_watchdog.h"

int main(void) {
    printf("[TEST] Testing net_watchdog anti-flap hysteresis logic...\n");

    net_link_state_t state = NET_STATE_DOWN;
    int success = 0;
    int failure = 0;
    bool changed = false;

    // 1. Initial State: DOWN
    assert(state == NET_STATE_DOWN);

    // 2. Feed 4 successful probes -> should stay in PROBING_UP, not yet UP
    for (int i = 1; i <= 4; i++) {
        state = net_watchdog_step_state(true, state, &success, &failure, &changed);
        assert(state == NET_STATE_PROBING_UP);
        assert(changed == false);
        assert(success == i);
    }

    // 3. 5th probe -> Transition to UP!
    state = net_watchdog_step_state(true, state, &success, &failure, &changed);
    assert(state == NET_STATE_UP);
    assert(changed == true);
    assert(success == 0);

    // 4. Stable UP state with successes
    state = net_watchdog_step_state(true, state, &success, &failure, &changed);
    assert(state == NET_STATE_UP);
    assert(changed == false);

    // 5. 1st Failure -> PROBING_DOWN, not yet DOWN
    state = net_watchdog_step_state(false, state, &success, &failure, &changed);
    assert(state == NET_STATE_PROBING_DOWN);
    assert(changed == false);
    assert(failure == 1);

    // 6. Temporary recovery -> Resets failure count, stays UP
    state = net_watchdog_step_state(true, state, &success, &failure, &changed);
    assert(state == NET_STATE_UP);
    assert(changed == false);
    assert(failure == 0);

    // 7. 2 consecutive failures -> PROBING_DOWN
    state = net_watchdog_step_state(false, state, &success, &failure, &changed);
    assert(state == NET_STATE_PROBING_DOWN);
    assert(failure == 1);

    state = net_watchdog_step_state(false, state, &success, &failure, &changed);
    assert(state == NET_STATE_PROBING_DOWN);
    assert(failure == 2);
    assert(changed == false);

    // 8. 3rd consecutive failure -> Transition to DOWN!
    state = net_watchdog_step_state(false, state, &success, &failure, &changed);
    assert(state == NET_STATE_DOWN);
    assert(changed == true);
    assert(failure == 0);

    printf("[PASS] Anti-flap hysteresis (3 fails / 5 restores) fully verified!\n");
    return 0;
}
