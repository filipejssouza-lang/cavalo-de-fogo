# 🐺 LupinOS Mojo AI Compute Pipeline & Cloud Forge

## Overview & Architecture

Mojo (Modular) delivers modern Python-like ergonomics with C++ / SIMD performance and deep MLIR lowering. However, deploying Mojo in sovereign mobile environments like **Fenrir (Xiaomi Mi 11 Ultra, Snapdragon 888 "star")** presents two fundamental obstacles:
1. **Toolchain Constraints:** Alpine Linux on mobile uses `musl libc`, while the Mojo compiler binary requires `glibc >= 2.34`.
2. **Resource Constraints:** Running JIT compilers or large LLVM optimization passes locally on mobile burns battery, triggers thermal throttling, and consumes precious RAM.

### Sovereign Cloud-Forge Solution
We decouple **compilation** from **execution**:
- **Cloud Forge (Google Colab / Jules / GitHub Actions):** Runs in ephemeral cloud Linux environments with full Modular CLI / Mojo support (`pip install mojo`). Compiles high-level Mojo kernels directly down to AArch64 machine code targeting `--target-triple aarch64-linux-gnu --mcpu cortex-x1 -O3 --emit object`.
- **Zero-Dependency C-ABI Object (`.o` / `.so`):** The output is a pure, self-contained ELF 64-bit object file exposing standard C symbols (`abi("c")`). It has zero runtime baggage beyond standard libc routines (`memset`).
- **Physical Mobile Execution (Fenrir):** The generated object file is linked locally on Alpine musl with GCC (`gcc -O3 bench_mojo.c lupin_mojo_cortex_x1.o -lm`) and executed directly on Snapdragon 888 silicon.

---

## Empirical Benchmark on Snapdragon 888 (Kryo 680 / Cortex-X1)

Tested on **Fenrir** pinned to **Prime Core 7 (Cortex-X1 @ 2.84 GHz)** under active Peltier cooling (32°C battery, 38°C CPU):

| Compute Kernel | Workload Dimension | Latency | Throughput / Efficiency |
|---|---|---|---|
| **`mojo_dot_f32`** | 1,000,000 floats (4 MB vector) | **0.724 ms** | **2.76 GFLOPS** (single-thread FP32) |
| **`mojo_gemm_f32`** | $256 \times 256$ Matrix Multiplication | **25.546 ms** | **1.31 GFLOPS** |
| **`mojo_rmsnorm_f32`** | Hidden Dimension = 4096 (LLaMA / Qwen) | **5.48 µs** | Microsecond per token |
| **`mojo_silu_hadamard_f32`** | MLP Dimension = 11008 (LLaMA-8B SwiGLU) | **63.76 µs** | 63 µs activation per token |

---

## Exporting C-ABI from Mojo 1.2+

In modern Mojo 1.2+, C-compatible functions are exported using the `@export("symbol_name")` decorator combined with the `abi("c")` function effect:

```mojo
from std.math import sqrt

@export("mojo_dot_f32")
def mojo_dot_f32(a_addr: Int, b_addr: Int, n: Int32) abi("c") -> Float32:
    var a = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_addr)
    var b = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_addr)
    var acc: Float32 = 0.0
    for i in range(Int(n)):
        acc += a[unsafe_offset=i] * b[unsafe_offset=i]
    return acc
```

### Key Technical Insights:
1. **`Pointer[Float32, MutUntrackedOrigin]`:** Uses the non-parametric `MutUntrackedOrigin` type to safely dereference raw integer memory addresses passed from C without triggering lifetime parameter errors.
2. **`unsafe_offset=i`:** Directly indexes consecutive memory blocks with zero runtime bounds-checking overhead.
3. **AAPCS64 Register Mapping:** In ARM64 calling conventions, `a_addr` is received in register `X0`, `b_addr` in `X1`, `n` in `W2`, and the return float is placed in register `S0`, exactly matching standard C:
   ```c
   float mojo_dot_f32(const float *a, const float *b, int n);
   ```

---

## Automating Builds via `colab-gpu-runner`

To forge a new kernel into the cluster without consuming PC RAM:
```bash
./tools/build_mojo_colab.py --src src/mojo/lupin_mojo_kernels.mojo --out-dir /path/to/output
```
The runner dispatches an ephemeral execution to Google Colab, compiles for Cortex-X1, verifies in QEMU, extracts the resulting `libmojokernels.so` and `lupin_mojo_cortex_x1.o` automatically via Base64 stdout patterns, and records quota telemetry in `~/.agents/memory/colab_ledger.json`.
