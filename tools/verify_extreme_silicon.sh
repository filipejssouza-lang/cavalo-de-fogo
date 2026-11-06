#!/bin/bash
set -e

echo "========================================================="
echo "🐺 BUILDING EXTREME SILICON BENCHMARK (Mi 11 Ultra)"
echo "========================================================="

# Ensure compilation uses aggressive ARMv8.4-A optimization
# and musl-gcc or clang for bare-metal portability
CC=${CC:-gcc}

CFLAGS="-O3 -static -mcpu=cortex-x1+dotprod -ffast-math -Wall"
LDFLAGS="-lm -lpthread -ldl"

mkdir -p build

echo "[1/2] Compiling sources..."
$CC $CFLAGS \
    src/cpu/neon_kernels.c \
    src/npu/fastrpc_cdsp.c \
    src/bench/bench_extreme.c \
    -o build/bench_extreme \
    $LDFLAGS

echo "[2/2] Build successful: build/bench_extreme"
echo "---------------------------------------------------------"
echo "To run the benchmark on the target hardware:"
echo "  ./build/bench_extreme"
echo "---------------------------------------------------------"

# If we are on aarch64 right now, we can run it
if [ "$(uname -m)" = "aarch64" ]; then
    echo "Running benchmark locally on aarch64..."
    ./build/bench_extreme
else
    echo "Note: Cross-compiled binary. Deploy to Mi 11 Ultra to execute."
    # Run it anyway to check if qemu-aarch64 is active or if we just fail.
    # We will ignore errors for execution to avoid failing the script on x86 CI.
    ./build/bench_extreme || true
fi
