import subprocess
import os
import base64
import sys

# Test script for Mojo SIMD vectorization on Cortex-X1
mojo_code = """
from memory import Pointer
from sys.ffi import abi

@export("mojo_dot_simd4")
def mojo_dot_simd4(a_ptr: Int, b_ptr: Int, n: Int) abi("c") -> Float32:
    var a = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_ptr)
    var b = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_ptr)
    var sum_vec = SIMD[DType.float32, 4](0.0)
    var i: Int = 0
    var n_simd: Int = n - (n % 4)

    while i < n_simd:
        var va = a.load[width=4](i)
        var vb = b.load[width=4](i)
        sum_vec += va * vb
        i += 4

    var total: Float32 = sum_vec.reduce_add()
    while i < n:
        total += a[i] * b[i]
        i += 1

    return total

@export("mojo_dot_scalar")
def mojo_dot_scalar(a_ptr: Int, b_ptr: Int, n: Int) abi("c") -> Float32:
    var a = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_ptr)
    var b = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_ptr)
    var total: Float32 = 0.0
    for i in range(n):
        total += a[i] * b[i]
    return total

@export("mojo_gemm_simd4")
def mojo_gemm_simd4(a_ptr: Int, b_ptr: Int, c_ptr: Int, M: Int, N: Int, K: Int) abi("c") -> None:
    var a = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=a_ptr)
    var b = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=b_ptr)
    var c = Pointer[Float32, MutUntrackedOrigin](unsafe_from_address=c_ptr)

    var K_simd: Int = K - (K % 4)
    for i in range(M):
        for j in range(N):
            var acc = SIMD[DType.float32, 4](0.0)
            var k: Int = 0
            while k < K_simd:
                var va = a.load[width=4](i * K + k)
                # B is transposed or row-major: for simplicity row-major inner loop
                acc += va * SIMD[DType.float32, 4](b[k * N + j], b[(k+1) * N + j], b[(k+2) * N + j], b[(k+3) * N + j])
                k += 4
            var s: Float32 = acc.reduce_add()
            while k < K:
                s += a[i * K + k] * b[k * N + j]
                k += 1
            c[i * N + j] = s
"""

with open("lupin_simd_kernels.mojo", "w") as f:
    f.write(mojo_code)

print("[1/4] Compiling Lupin SIMD Mojo kernels for Cortex-X1...")
cmd = [
    "mojo", "build",
    "--target-triple", "aarch64-linux-gnu",
    "--mcpu", "cortex-x1",
    "-O3",
    "--emit", "object",
    "lupin_simd_kernels.mojo",
    "-o", "lupin_simd_kernels.o"
]
res = subprocess.run(cmd, capture_output=True, text=True)
print("STDOUT:", res.stdout)
print("STDERR:", res.stderr)
if res.returncode != 0:
    print("Mojo build failed!")
    sys.exit(1)

print("[2/4] Linking shared library for AArch64...")
# Build .so
cmd_so = [
    "aarch64-linux-gnu-gcc", "-shared", "-fPIC", "-O3",
    "lupin_simd_kernels.o", "-o", "libmojosimd.so"
]
subprocess.run(cmd_so, check=True)

# Build static QEMU test harness
c_test = """
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

extern float mojo_dot_simd4(const float *a, const float *b, int n);
extern float mojo_dot_scalar(const float *a, const float *b, int n);

int main() {
    int n = 1024;
    float *a = (float*)malloc(n * sizeof(float));
    float *b = (float*)malloc(n * sizeof(float));
    for (int i = 0; i < n; i++) {
        a[i] = 1.0f;
        b[i] = 2.0f;
    }
    float s_simd = mojo_dot_simd4(a, b, n);
    float s_scalar = mojo_dot_scalar(a, b, n);
    printf("QEMU Verification: SIMD=%f, Scalar=%f (Expected: %f)\\n", s_simd, s_scalar, (float)n * 2.0f);
    if (fabs(s_simd - 2048.0f) < 1e-3) {
        printf("VERIFICATION PASSED!\\n");
        return 0;
    } else {
        printf("VERIFICATION FAILED!\\n");
        return 1;
    }
}
"""
with open("test_harness.c", "w") as f:
    f.write(c_test)

cmd_test = [
    "aarch64-linux-gnu-gcc", "-static", "-O3",
    "test_harness.c", "lupin_simd_kernels.o", "-o", "test_harness_aarch64"
]
subprocess.run(cmd_test, check=True)

print("[3/4] Running test in QEMU user emulation...")
res_qemu = subprocess.run(["qemu-aarch64", "./test_harness_aarch64"], capture_output=True, text=True)
print("QEMU Output:", res_qemu.stdout)
if "VERIFICATION PASSED" not in res_qemu.stdout:
    print("QEMU execution failed!")
    sys.exit(1)

print("[4/4] Packing artifacts as Base64 for extraction...")
for fname in ["lupin_simd_kernels.o", "libmojosimd.so"]:
    if os.path.exists(fname):
        with open(fname, "rb") as f:
            b64 = base64.b64encode(f.read()).decode("ascii")
            print(f"__FILE_BASE64:{fname}:{b64}__")

print("All SIMD compilation steps completed successfully!")
