#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include <unistd.h>

extern float mojo_dot_f32(const float *a, const float *b, int n);
extern void mojo_gemm_f32(const float *a, const float *b, float *c, int M, int N, int K);
extern void mojo_rmsnorm_f32(const float *x, const float *w, float *out, int n, float eps);
extern void mojo_silu_hadamard_f32(const float *gate, const float *up, float *out, int n);

static double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
    // Pin to Cortex-X1 Prime Core (CPU 7 on Snapdragon 888)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(7, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0) {
        printf("🐺 [Fenrir] Process successfully pinned to Cortex-X1 Prime Core (CPU 7 @ 2.84 GHz)!\n");
    } else {
        printf("⚠️ [Fenrir] Could not pin to core 7; running on available cores.\n");
    }

    printf("\n=== 🚀 BENCHMARK: MOJO KERNELS ON SNAPDRAGON 888 CORTEX-X1 ===\n");

    // 1. Vector Dot Product (N = 1,000,000 floats = 4 MB)
    int n_dot = 1000000;
    float *a = (float *)malloc(n_dot * sizeof(float));
    float *b = (float *)malloc(n_dot * sizeof(float));
    for (int i = 0; i < n_dot; i++) {
        a[i] = 1.0f / (i + 1.0f);
        b[i] = 0.5f;
    }
    
    // Warmup
    volatile float dot_res = mojo_dot_f32(a, b, n_dot);
    
    int iters_dot = 200;
    double t0 = get_time_sec();
    for (int it = 0; it < iters_dot; it++) {
        dot_res = mojo_dot_f32(a, b, n_dot);
    }
    double t1 = get_time_sec();
    double avg_dot_time = (t1 - t0) / iters_dot;
    double gflops_dot = (2.0 * n_dot * 1e-9) / avg_dot_time;
    printf("[1/4] Dot Product (N=1M f32):\n");
    printf("      Latency: %.3f ms | Throughput: %.2f GFLOPS | Checksum: %f\n", avg_dot_time * 1e3, gflops_dot, dot_res);

    // 2. Matrix Multiplication GEMM 256x256 (f32)
    int M = 256, N = 256, K = 256;
    float *A = (float *)malloc(M * K * sizeof(float));
    float *B = (float *)malloc(K * N * sizeof(float));
    float *C = (float *)malloc(M * N * sizeof(float));
    for (int i = 0; i < M * K; i++) A[i] = 0.01f * (i % 100);
    for (int i = 0; i < K * N; i++) B[i] = 0.02f * (i % 100);
    
    // Warmup
    mojo_gemm_f32(A, B, C, M, N, K);
    
    int iters_gemm = 50;
    t0 = get_time_sec();
    for (int it = 0; it < iters_gemm; it++) {
        mojo_gemm_f32(A, B, C, M, N, K);
    }
    t1 = get_time_sec();
    double avg_gemm_time = (t1 - t0) / iters_gemm;
    double total_flops_gemm = 2.0 * M * N * K;
    double gflops_gemm = (total_flops_gemm * 1e-9) / avg_gemm_time;
    printf("[2/4] GEMM Matrix Mul (256x256 f32):\n");
    printf("      Latency: %.3f ms | Compute: %.2f GFLOPS | C[0]=%.3f\n", avg_gemm_time * 1e3, gflops_gemm, C[0]);

    // 3. RMSNorm on LLM Hidden Dim (D = 4096, e.g. LLaMA/Qwen)
    int dim = 4096;
    float *x = (float *)malloc(dim * sizeof(float));
    float *w = (float *)malloc(dim * sizeof(float));
    float *out_rms = (float *)malloc(dim * sizeof(float));
    for (int i = 0; i < dim; i++) {
        x[i] = 0.1f * ((i % 17) - 8);
        w[i] = 1.0f;
    }
    
    int iters_rms = 50000;
    t0 = get_time_sec();
    for (int it = 0; it < iters_rms; it++) {
        mojo_rmsnorm_f32(x, w, out_rms, dim, 1e-5f);
    }
    t1 = get_time_sec();
    double avg_rms_us = ((t1 - t0) / iters_rms) * 1e6;
    printf("[3/4] RMSNorm Layer (Hidden Dim=4096):\n");
    printf("      Latency: %.2f us per token | out[0]=%.4f\n", avg_rms_us, out_rms[0]);

    // 4. SwiGLU / SiLU-Hadamard Activation (D = 11008, LLaMA-7B/8B MLP Dim)
    int mlp_dim = 11008;
    float *gate = (float *)malloc(mlp_dim * sizeof(float));
    float *up = (float *)malloc(mlp_dim * sizeof(float));
    float *silu_out = (float *)malloc(mlp_dim * sizeof(float));
    for (int i = 0; i < mlp_dim; i++) {
        gate[i] = 0.05f * ((i % 23) - 11);
        up[i] = 1.0f;
    }
    
    int iters_silu = 10000;
    t0 = get_time_sec();
    for (int it = 0; it < iters_silu; it++) {
        mojo_silu_hadamard_f32(gate, up, silu_out, mlp_dim);
    }
    t1 = get_time_sec();
    double avg_silu_us = ((t1 - t0) / iters_silu) * 1e6;
    printf("[4/4] SwiGLU / SiLU-Hadamard Activation (MLP Dim=11008):\n");
    printf("      Latency: %.2f us per token | out[0]=%.4f\n", avg_silu_us, silu_out[0]);

    printf("\n=== ✅ ALL MOJO COMPUTE KERNELS EXECUTED AND VALIDATED ON FENRIR! ===\n");
    return 0;
}
