# 🐺 Lupin Bare-Metal OS v3: Qualcomm Silicon Architecture

## The Extreme Silicon Layer

LupinOS v3 completely discards software emulation and cloud stubs, talking directly to the physical silicon on the Snapdragon 888 (SM8350). The architecture is divided across three heterogeneous computing blocks:

### 1. Hexagon 780 NPU (cDSP) via FastRPC
Instead of relying on high-level Android NNAPI, LupinOS v3 uses direct `ioctl` calls to `/dev/msm_fastrpc-2`.
- **Dynamic Linking:** Directly loads `/vendor_real/lib64/libcdsprpc.so` from the device's real vendor partition.
- **Zero-Copy Memory:** Leverages `/dev/ion` (or direct DMA-BUF) to map memory directly between the CPU and the cDSP. This prevents costly memory copies of large tensor data during LLM inference.
- **Role:** Handles Gating & Mixture of Experts (MoE) routing, providing up to 26 TOPS of INT8 compute at extremely low power (~2W).

### 2. Cortex-X1 Prime Core (CPU)
The CPU cluster's crown jewel is the Kryo 680 Prime Core (Cortex-X1 @ 2.84 GHz).
- **Core Binding:** The inference engine strictly binds threads to Core 7 using `sched_setaffinity` to guarantee maximum single-thread performance without OS scheduler migrations.
- **ARMv8.4-A Dot Product:** Employs explicit SIMD vectorization via `<arm_neon.h>`. Functions like `neon_dotprod_i8_i32` use the `sdot` instruction to perform four 8-bit multiply-accumulate operations in a single CPU cycle.
- **Role:** Handles sequential, memory-bound tasks like Token-by-Token generation, RMSNorm, and SwiGLU activations, minimizing latency.

### 3. Adreno 660 GPU
(Already established via Mesa Turnip Vulkan 1.3).
- **Role:** Massively parallel prompt ingestion (Prefill phase) utilizing the 51.2 GB/s LPDDR5 bandwidth.

## The Native Guardian (`lupinos_guard`)
A statically compiled, zero-dependency C binary that orchestrates this hardware:
- Ensures the AMOLED display is kept totally off to prevent burn-in and save power.
- Provisions 12GB of ZRAM (Swap) directly using `sysconf` and the `swapon` syscall.
- Exports proper environment variables (`LD_LIBRARY_PATH`, `ADSP_LIBRARY_PATH`) so standard Linux environments can resolve the proprietary Qualcomm DSP libraries located in `/vendor_real`.
- Monitors thermal zones (`/sys/class/thermal`) to guarantee the external Peltier cooler maintains the SoC well below throttling thresholds (< 65°C).

## Zero-Stubs Directive
This layer contains NO dummy files. It expects and interacts with the real device nodes and shared objects present on a physically compromised and rooted Xiaomi Mi 11 Ultra.
