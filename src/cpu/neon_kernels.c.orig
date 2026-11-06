#define _GNU_SOURCE
#include "neon_kernels.h"
#include <arm_neon.h>
#include <math.h>
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

void neon_rmsnorm_f32(float* out, const float* in, const float* weight, int dim, float eps) {
    float sum_sq = 0.0f;
    float32x4_t v_sum_sq = vdupq_n_f32(0.0f);
    
    int i = 0;
    // Unrolled by 4 x 4 (16 floats per loop)
    for (; i <= dim - 16; i += 16) {
        float32x4_t v0 = vld1q_f32(&in[i]);
        float32x4_t v1 = vld1q_f32(&in[i + 4]);
        float32x4_t v2 = vld1q_f32(&in[i + 8]);
        float32x4_t v3 = vld1q_f32(&in[i + 12]);
        
        v_sum_sq = vmlaq_f32(v_sum_sq, v0, v0);
        v_sum_sq = vmlaq_f32(v_sum_sq, v1, v1);
        v_sum_sq = vmlaq_f32(v_sum_sq, v2, v2);
        v_sum_sq = vmlaq_f32(v_sum_sq, v3, v3);
    }
    
    // Process remaining elements (dim might not be multiple of 16)
    for (; i < dim; i++) {
        sum_sq += in[i] * in[i];
    }
    
    // Horizontal add of the NEON register
    float sum_arr[4];
    vst1q_f32(sum_arr, v_sum_sq);
    sum_sq += sum_arr[0] + sum_arr[1] + sum_arr[2] + sum_arr[3];
    
    float mean_sq = sum_sq / dim;
    float inv_r = 1.0f / sqrtf(mean_sq + eps);
    float32x4_t v_inv_r = vdupq_n_f32(inv_r);
    
    i = 0;
    for (; i <= dim - 16; i += 16) {
        float32x4_t v_w0 = vld1q_f32(&weight[i]);
        float32x4_t v_in0 = vld1q_f32(&in[i]);
        float32x4_t v_out0 = vmulq_f32(vmulq_f32(v_in0, v_inv_r), v_w0);
        vst1q_f32(&out[i], v_out0);
        
        float32x4_t v_w1 = vld1q_f32(&weight[i + 4]);
        float32x4_t v_in1 = vld1q_f32(&in[i + 4]);
        float32x4_t v_out1 = vmulq_f32(vmulq_f32(v_in1, v_inv_r), v_w1);
        vst1q_f32(&out[i + 4], v_out1);
        
        float32x4_t v_w2 = vld1q_f32(&weight[i + 8]);
        float32x4_t v_in2 = vld1q_f32(&in[i + 8]);
        float32x4_t v_out2 = vmulq_f32(vmulq_f32(v_in2, v_inv_r), v_w2);
        vst1q_f32(&out[i + 8], v_out2);
        
        float32x4_t v_w3 = vld1q_f32(&weight[i + 12]);
        float32x4_t v_in3 = vld1q_f32(&in[i + 12]);
        float32x4_t v_out3 = vmulq_f32(vmulq_f32(v_in3, v_inv_r), v_w3);
        vst1q_f32(&out[i + 12], v_out3);
    }
    
    for (; i < dim; i++) {
        out[i] = (in[i] * inv_r) * weight[i];
    }
}

// Helper for fast sigmoid using NEON
static inline float32x4_t v_sigmoid(float32x4_t v) {
    // For a highly optimized bare-metal version, we'd use min-max approximations
    // or lookup tables for expf.
    // For simplicity, we extract and use scalar expf, then repack, or a rough polynomial.
    // Let's do the scalar fallback for now inside the vector loop just to keep it readable,
    // though in production we'd use a vectorized expf approximation.
    float arr[4];
    vst1q_f32(arr, v);
    arr[0] = 1.0f / (1.0f + expf(-arr[0]));
    arr[1] = 1.0f / (1.0f + expf(-arr[1]));
    arr[2] = 1.0f / (1.0f + expf(-arr[2]));
    arr[3] = 1.0f / (1.0f + expf(-arr[3]));
    return vld1q_f32(arr);
}

void neon_swiglu_f32(float* out, const float* in1, const float* in2, int dim) {
    int i = 0;
    for (; i <= dim - 4; i += 4) {
        float32x4_t v1 = vld1q_f32(&in1[i]);
        float32x4_t v2 = vld1q_f32(&in2[i]);
        
        // SiLU(v2) = v2 * sigmoid(v2)
        float32x4_t sig_v2 = v_sigmoid(v2);
        float32x4_t silu_v2 = vmulq_f32(v2, sig_v2);
        
        // v1 * SiLU(v2)
        float32x4_t res = vmulq_f32(v1, silu_v2);
        vst1q_f32(&out[i], res);
    }
    
    for (; i < dim; i++) {
        float sig = 1.0f / (1.0f + expf(-in2[i]));
        out[i] = in1[i] * (in2[i] * sig);
    }
}

void neon_dotprod_i8_i32(int32_t* c, const int8_t* a, const int8_t* b, int M, int N, int K) {
    // b must be transposed for efficient row-wise reading: [N, K]
    // A: [M, K]
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            int32x4_t v_acc = vdupq_n_s32(0);
            int32_t acc = 0;
            
            int k = 0;
            for (; k <= K - 16; k += 16) {
                int8x16_t va = vld1q_s8(&a[i * K + k]);
                int8x16_t vb = vld1q_s8(&b[j * K + k]);
                
                // Use ARMv8.4-A dot product instruction
                // vdotq_s32 computes four 32-bit dot products of 4x8-bit chunks
                v_acc = vdotq_s32(v_acc, va, vb);
            }
            
            // Horizontal add of the NEON register
            int32_t sum_arr[4];
            vst1q_s32(sum_arr, v_acc);
            acc = sum_arr[0] + sum_arr[1] + sum_arr[2] + sum_arr[3];
            
            // Remainder
            for (; k < K; k++) {
                acc += (int32_t)a[i * K + k] * (int32_t)b[j * K + k];
            }
            
            c[i * N + j] = acc;
        }
    }
}

bool bind_to_cortex_x1(void) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    // Core 7 is the Cortex-X1 Prime Core on Snapdragon 888 (SM8350)
    CPU_SET(7, &cpuset);
    
    int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (s != 0) {
        fprintf(stderr, "[CPU] Warning: Failed to bind to Cortex-X1 (Core 7). Are you running with appropriate privileges?\n");
        return false;
    }
    
    printf("[CPU] Successfully bound thread to Cortex-X1 (Core 7).\n");
    return true;
}
