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

echo "All tests passed (Compilation OK)."
