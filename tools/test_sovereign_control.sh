#!/bin/bash
set -e

echo "Building binaries..."
make clean
make

echo ""
echo "Running tests..."
echo "1. Network Watchdog (compile check)"
if [ -f "build/lupinos_guard" ]; then
    echo "[PASS] lupinos_guard compiled successfully"
else
    echo "[FAIL] lupinos_guard not found"
    exit 1
fi

echo "2. CPU Power Control CLI"
if [ -f "build/lupin-power" ]; then
    echo "[PASS] lupin-power compiled successfully"
else
    echo "[FAIL] lupin-power not found"
    exit 1
fi

echo "3. DRM Subdisplay HUD"
if [ -f "build/drm_subdisplay" ]; then
    echo "[PASS] drm_subdisplay compiled successfully"
else
    echo "[FAIL] drm_subdisplay not found"
    exit 1
fi

echo "4. Governor Daemon Thermal Policy Logic"
export LUPIN_MOCK_CPU_TEMP_NODE="/tmp/mock_cpu_temp"
export LUPIN_MOCK_BAT_TEMP_NODE="/tmp/mock_bat_temp"
export LUPIN_MOCK_COOLING_NODE="/tmp/mock_cooling"

echo 40000 > "$LUPIN_MOCK_CPU_TEMP_NODE"
echo 300 > "$LUPIN_MOCK_BAT_TEMP_NODE"
echo 0 > "$LUPIN_MOCK_COOLING_NODE"
rm -f /tmp/lupin_thermal_state

# We test the daemon's loop iteration by injecting a small script to wrap it and run it for 1 iteration,
# but since it's a while(1) loop in C, we compile a small test harness or just let it run in background and observe
cat << 'EOF' > tools/test_thermal_policy.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern void governor_daemon_loop(void);

void *timeout_thread(void *arg) {
    sleep(4); // Run daemon for 4 seconds
    exit(0);
    return NULL;
}

int main() {
    pthread_t t;
    pthread_create(&t, NULL, timeout_thread, NULL);
    governor_daemon_loop();
    return 0;
}
EOF
gcc -Isrc/cpu -c src/cpu/governor_daemon.c -o build/governor_daemon.o
gcc -Isrc/cpu -c src/cpu/governor_control.c -o build/governor_control.o
gcc tools/test_thermal_policy.c build/governor_daemon.o build/governor_control.o -o build/test_thermal_policy -pthread
./build/test_thermal_policy &
TEST_PID=$!

sleep 1
STATE=$(cat /tmp/lupin_thermal_state)
if [ "$STATE" != "BALANCED" ]; then
    echo "[FAIL] Expected BALANCED for normal temps, got $STATE"
    kill $TEST_PID
    exit 1
fi
echo "[PASS] Governor logic initialized to BALANCED for normal temps"

# Simulate overheat
echo 70000 > "$LUPIN_MOCK_CPU_TEMP_NODE"
sleep 2.5
STATE=$(cat /tmp/lupin_thermal_state)
COOLING=$(cat "$LUPIN_MOCK_COOLING_NODE")
if [ "$STATE" != "IDLE (THERMAL THROTTLE)" ]; then
    echo "[FAIL] Expected IDLE (THERMAL THROTTLE) for overheat, got $STATE"
    kill $TEST_PID
    exit 1
fi
if [ "$COOLING" != "1" ]; then
    echo "[FAIL] Expected cooling node to be 1 for overheat, got $COOLING"
    kill $TEST_PID
    exit 1
fi
echo "[PASS] Governor logic transitioned to IDLE and activated cooling for overheat"

wait $TEST_PID || true
rm -f tools/test_thermal_policy.c build/test_thermal_policy
rm -f "$LUPIN_MOCK_CPU_TEMP_NODE" "$LUPIN_MOCK_BAT_TEMP_NODE" "$LUPIN_MOCK_COOLING_NODE"

echo "All tests passed (Compilation OK and Thermal Policy Logic Verified)."
