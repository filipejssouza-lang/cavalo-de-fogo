#ifndef NEON_KERNELS_H
#define NEON_KERNELS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Vectorized 4x-unrolled RMSNorm for Hidden Dim
// dim: dimension of the hidden state (must be multiple of 16 for best performance)
void neon_rmsnorm_f32(float* out, const float* in, const float* weight, int dim, float eps);

// Vectorized SiLU-Hadamard activation for MLP Dim
// Computes: out = in1 * (in2 / (1 + exp(-in2)))
// Used in SwiGLU block.
void neon_swiglu_f32(float* out, const float* in1, const float* in2, int dim);

// 8-bit quantized matrix dot product using ARMv8.4-A Dot Product
// Requires CPU supporting dotprod (like Cortex-X1)
// a: int8_t matrix of shape [M, K]
// b: int8_t matrix of shape [N, K]
// c: int32_t output of shape [M, N]
void neon_dotprod_i8_i32(int32_t* c, const int8_t* a, const int8_t* b, int M, int N, int K);

// Bind the current thread to Cortex-X1 Core 7 (Prime Core)
// Requires sched_setaffinity
bool bind_to_cortex_x1(void);

#endif // NEON_KERNELS_H
