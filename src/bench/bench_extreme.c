#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "../cpu/neon_kernels.h"
#include "../npu/fastrpc_cdsp.h"

static double get_time_in_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

int main() {
    printf("=========================================================\n");
    printf("🐺 LUPIN V3 EXTREME SILICON BENCHMARK\n");
    printf("=========================================================\n\n");

    // 1. Bind to Cortex-X1
    printf("[1/4] Binding to Cortex-X1 Core 7...\n");
    bind_to_cortex_x1();
    printf("Done.\n\n");

    // 2. Test NEON RMSNorm (Hidden Dim = 4096)
    int rmsnorm_dim = 4096;
    float* rms_in = (float*)aligned_alloc(32, rmsnorm_dim * sizeof(float));
    float* rms_weight = (float*)aligned_alloc(32, rmsnorm_dim * sizeof(float));
    float* rms_out = (float*)aligned_alloc(32, rmsnorm_dim * sizeof(float));

    for (int i = 0; i < rmsnorm_dim; i++) {
        rms_in[i] = (float)i / rmsnorm_dim;
        rms_weight[i] = 1.0f;
    }

    printf("[2/4] Benchmarking NEON RMSNorm (Dim: %d)...\n", rmsnorm_dim);
    double t_start = get_time_in_ms();
    int iterations = 10000;
    for (int i = 0; i < iterations; i++) {
        neon_rmsnorm_f32(rms_out, rms_in, rms_weight, rmsnorm_dim, 1e-5f);
    }
    double t_end = get_time_in_ms();
    double rmsnorm_time_us = ((t_end - t_start) * 1000.0) / iterations;
    printf("  -> Avg Latency: %.2f us/token\n", rmsnorm_time_us);
    
    free(rms_in);
    free(rms_weight);
    free(rms_out);
    printf("Done.\n\n");

    // 3. Test NEON INT8 DotProd (M=1, N=4096, K=4096 for a standard FC layer)
    int M = 1;
    int N = 4096;
    int K = 4096;
    int8_t* dp_a = (int8_t*)aligned_alloc(32, M * K * sizeof(int8_t));
    int8_t* dp_b = (int8_t*)aligned_alloc(32, N * K * sizeof(int8_t));
    int32_t* dp_c = (int32_t*)aligned_alloc(32, M * N * sizeof(int32_t));

    for (int i = 0; i < M * K; i++) dp_a[i] = i % 127;
    for (int i = 0; i < N * K; i++) dp_b[i] = i % 127;

    printf("[3/4] Benchmarking NEON INT8 DotProd (M=%d, N=%d, K=%d)...\n", M, N, K);
    t_start = get_time_in_ms();
    iterations = 1000;
    for (int i = 0; i < iterations; i++) {
        neon_dotprod_i8_i32(dp_c, dp_a, dp_b, M, N, K);
    }
    t_end = get_time_in_ms();
    double dp_time_us = ((t_end - t_start) * 1000.0) / iterations;
    double gflops = (2.0 * M * N * K) / (dp_time_us * 1000.0); // 2 ops per MAC
    printf("  -> Avg Latency: %.2f us/token\n", dp_time_us);
    printf("  -> Performance: %.2f GFLOPS\n", gflops);

    free(dp_a);
    free(dp_b);
    free(dp_c);
    printf("Done.\n\n");

    // 4. Test FastRPC Interface
    printf("[4/4] Testing FastRPC cDSP Interface...\n");
    if (fastrpc_cdsp_init()) {
        int fd_in = -1;
        void* dma_buf = fastrpc_cdsp_alloc_buffer(4096, &fd_in);
        if (dma_buf) {
            printf("  -> DMA-BUF allocated successfully (FD: %d).\n", fd_in);
            
            t_start = get_time_in_ms();
            bool rpc_success = fastrpc_cdsp_execute_inference(dma_buf, 4096, dma_buf, 4096);
            t_end = get_time_in_ms();
            
            if (rpc_success) {
                printf("  -> FastRPC Invocation: SUCCESS\n");
                printf("  -> FastRPC Round-Trip Latency: %.2f ms\n", t_end - t_start);
            } else {
                printf("  -> FastRPC Invocation: FAILED\n");
            }
            fastrpc_cdsp_free_buffer(dma_buf, 4096, fd_in);
        } else {
            printf("  -> DMA-BUF allocation failed or skipped (no /dev/ion).\n");
        }
        fastrpc_cdsp_cleanup();
    } else {
        printf("  -> FastRPC init failed (No /dev/msm_fastrpc-2 accessible).\n");
    }
    printf("Done.\n\n");

    printf("=========================================================\n");
    printf("ALL TESTS COMPLETED.\n");
    printf("=========================================================\n");
    return 0;
}
