#define _GNU_SOURCE
#include "neon_kernels.h"
#include <math.h>
#include <sched.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>

/*
 * Fast SIMD vectorized expf(x) for ARM NEON (4x FP32 lanes)
 * Range reduction: exp(x) = 2^k * exp(r), where r = x - k * ln(2), |r| <= 0.5 * ln(2)
 */
static inline float32x4_t v_expf_neon(float32x4_t x) {
    const float32x4_t v_max   = vdupq_n_f32(88.3762626647949f);
    const float32x4_t v_min   = vdupq_n_f32(-87.336544750402f);
    const float32x4_t v_log2e = vdupq_n_f32(1.44269504088896341f);
    const float32x4_t v_ln2_hi = vdupq_n_f32(0.693359375f);
    const float32x4_t v_ln2_lo = vdupq_n_f32(-2.12194440e-4f);

    const float32x4_t c1 = vdupq_n_f32(1.0f);
    const float32x4_t c2 = vdupq_n_f32(0.5f);
    const float32x4_t c3 = vdupq_n_f32(0.1666666716337204f);
    const float32x4_t c4 = vdupq_n_f32(0.0416666679084301f);
    const float32x4_t c5 = vdupq_n_f32(0.0083333337679505f);

    /* Clamp input to avoid IEEE-754 overflow/underflow */
    x = vminq_f32(vmaxq_f32(x, v_min), v_max);

    /* k = round(x * log2(e)) */
    float32x4_t fx = vrndnq_f32(vmulq_f32(x, v_log2e));

    /* r = x - fx * ln2_hi - fx * ln2_lo */
    float32x4_t r = vfmsq_f32(x, fx, v_ln2_hi);
    r = vfmsq_f32(r, fx, v_ln2_lo);

    /* Horner polynomial for exp(r) */
    float32x4_t y = vfmaq_f32(c4, c5, r);
    y = vfmaq_f32(c3, y, r);
    y = vfmaq_f32(c2, y, r);
    y = vfmaq_f32(c1, y, r);
    y = vfmaq_f32(c1, y, r);

    /* Scale by 2^k via IEEE-754 exponent bit shift */
    int32x4_t k = vcvtq_s32_f32(fx);
    int32x4_t pow2k = vshlq_n_s32(vaddq_s32(k, vdupq_n_s32(127)), 23);
    return vmulq_f32(y, vreinterpretq_f32_s32(pow2k));
}

/* Vectorized NEON sigmoid: 1.0f / (1.0f + exp(-v)) with Newton-Raphson reciprocal refinement */
static inline float32x4_t v_sigmoid(float32x4_t v) {
    float32x4_t exp_neg = v_expf_neon(vnegq_f32(v));
    float32x4_t denom   = vaddq_f32(vdupq_n_f32(1.0f), exp_neg);
    float32x4_t recip   = vrecpeq_f32(denom);
    recip = vmulq_f32(vrecpsq_f32(denom, recip), recip);
    recip = vmulq_f32(vrecpsq_f32(denom, recip), recip);
    return recip;
}

void neon_rmsnorm_f32(float* out, const float* in, const float* weight, int dim, float eps) {
    float sum_sq = 0.0f;
    float32x4_t v_sum_sq = vdupq_n_f32(0.0f);

    int i = 0;
    for (; i <= dim - 16; i += 16) {
        float32x4_t v0 = vld1q_f32(&in[i]);
        float32x4_t v1 = vld1q_f32(&in[i + 4]);
        float32x4_t v2 = vld1q_f32(&in[i + 8]);
        float32x4_t v3 = vld1q_f32(&in[i + 12]);

        v_sum_sq = vfmaq_f32(v_sum_sq, v0, v0);
        v_sum_sq = vfmaq_f32(v_sum_sq, v1, v1);
        v_sum_sq = vfmaq_f32(v_sum_sq, v2, v2);
        v_sum_sq = vfmaq_f32(v_sum_sq, v3, v3);
    }

    for (; i < dim; i++) {
        sum_sq += in[i] * in[i];
    }

    sum_sq += vaddvq_f32(v_sum_sq);

    float mean_sq = sum_sq / (float)dim;
    float inv_r = 1.0f / sqrtf(mean_sq + eps);
    float32x4_t v_inv_r = vdupq_n_f32(inv_r);

    i = 0;
    for (; i <= dim - 16; i += 16) {
        float32x4_t v_w0 = vld1q_f32(&weight[i]);
        float32x4_t v_in0 = vld1q_f32(&in[i]);
        vst1q_f32(&out[i], vmulq_f32(vmulq_f32(v_in0, v_inv_r), v_w0));

        float32x4_t v_w1 = vld1q_f32(&weight[i + 4]);
        float32x4_t v_in1 = vld1q_f32(&in[i + 4]);
        vst1q_f32(&out[i + 4], vmulq_f32(vmulq_f32(v_in1, v_inv_r), v_w1));

        float32x4_t v_w2 = vld1q_f32(&weight[i + 8]);
        float32x4_t v_in2 = vld1q_f32(&in[i + 8]);
        vst1q_f32(&out[i + 8], vmulq_f32(vmulq_f32(v_in2, v_inv_r), v_w2));

        float32x4_t v_w3 = vld1q_f32(&weight[i + 12]);
        float32x4_t v_in3 = vld1q_f32(&in[i + 12]);
        vst1q_f32(&out[i + 12], vmulq_f32(vmulq_f32(v_in3, v_inv_r), v_w3));
    }

    for (; i < dim; i++) {
        out[i] = (in[i] * inv_r) * weight[i];
    }
}

void neon_swiglu_f32(float* out, const float* in1, const float* in2, int dim) {
    int i = 0;
    for (; i <= dim - 4; i += 4) {
        float32x4_t v1 = vld1q_f32(&in1[i]);
        float32x4_t v2 = vld1q_f32(&in2[i]);

        float32x4_t sig_v2  = v_sigmoid(v2);
        float32x4_t silu_v2 = vmulq_f32(v2, sig_v2);
        float32x4_t res     = vmulq_f32(v1, silu_v2);
        vst1q_f32(&out[i], res);
    }

    for (; i < dim; i++) {
        float sig = 1.0f / (1.0f + expf(-in2[i]));
        out[i] = in1[i] * (in2[i] * sig);
    }
}

void neon_dotprod_i8_i32(int32_t* c, const int8_t* a, const int8_t* b, int M, int N, int K) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            int32x4_t v_acc = vdupq_n_s32(0);
            int k = 0;
            for (; k <= K - 16; k += 16) {
                int8x16_t va = vld1q_s8(&a[i * K + k]);
                int8x16_t vb = vld1q_s8(&b[j * K + k]);
                v_acc = vdotq_s32(v_acc, va, vb);
            }
            int32_t acc = vaddvq_s32(v_acc);
            for (; k < K; k++) {
                acc += (int32_t)a[i * K + k] * (int32_t)b[j * K + k];
            }
            c[i * N + j] = acc;
        }
    }
}

#else /* Portable fallback for non-ARM64 CI/host verification */

void neon_rmsnorm_f32(float* out, const float* in, const float* weight, int dim, float eps) {
    float sum_sq = 0.0f;
    for (int i = 0; i < dim; i++) {
        sum_sq += in[i] * in[i];
    }
    float inv_r = 1.0f / sqrtf((sum_sq / (float)dim) + eps);
    for (int i = 0; i < dim; i++) {
        out[i] = (in[i] * inv_r) * weight[i];
    }
}

void neon_swiglu_f32(float* out, const float* in1, const float* in2, int dim) {
    for (int i = 0; i < dim; i++) {
        float sig = 1.0f / (1.0f + expf(-in2[i]));
        out[i] = in1[i] * (in2[i] * sig);
    }
}

void neon_dotprod_i8_i32(int32_t* c, const int8_t* a, const int8_t* b, int M, int N, int K) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            int32_t acc = 0;
            for (int k = 0; k < K; k++) {
                acc += (int32_t)a[i * K + k] * (int32_t)b[j * K + k];
            }
            c[i * N + j] = acc;
        }
    }
}
#endif

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
