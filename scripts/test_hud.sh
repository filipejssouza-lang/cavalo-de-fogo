#!/bin/bash
set -e

echo "[+] Building targets..."
make build/drm_subdisplay
make build/lupinctl

echo "[+] Starting HUD in background..."
./build/drm_subdisplay &
HUD_PID=$!

# Give it a second to start and bind socket
sleep 1

echo "[+] Testing 'next' command..."
./build/lupinctl hud next
sleep 1

echo "[+] Testing 'status' command..."
./build/lupinctl hud status
sleep 1

echo "[+] Testing 'dim' command..."
./build/lupinctl hud dim
sleep 1

echo "[+] Testing 'on' command..."
./build/lupinctl hud on
sleep 1

echo "[+] Stopping HUD..."
kill -SIGINT $HUD_PID
wait $HUD_PID || true

echo "[+] Test completed successfully."
