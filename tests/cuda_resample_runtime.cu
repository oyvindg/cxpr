#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

/* Produced by test_cuda_index_contract with CXPR_CUDA_SOURCE_OUT. */
#ifndef CXPR_CUDA_RESAMPLE_GENERATED
#define CXPR_CUDA_RESAMPLE_GENERATED "cxpr_resample_generated.cu"
#endif
#include CXPR_CUDA_RESAMPLE_GENERATED

__global__ static void run_resample(
    cxpr_model_tick_state* state,
    const double* inputs,
    double* outputs,
    const cxpr_resample_view* views,
    size_t primary_cursor) {
    cxpr_model_tick(state, inputs, nullptr, outputs, views, primary_cursor);
}

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

static void run_and_expect(cxpr_model_tick_state* state, const double* inputs,
                           double* outputs, const cxpr_resample_view* views,
                           size_t cursor, double expected, const char* scenario) {
    double actual = NAN;
    run_resample<<<1, 1>>>(state, inputs, outputs, views, cursor);
    check(cudaGetLastError(), "launch resample kernel");
    check(cudaDeviceSynchronize(), "synchronize resample kernel");
    check(cudaMemcpy(&actual, outputs, sizeof(actual), cudaMemcpyDeviceToHost),
          "copy output");
    if ((std::isnan(expected) && !std::isnan(actual)) ||
        (!std::isnan(expected) && actual != expected)) {
        std::fprintf(stderr, "CUDA %s mismatch: got %.17g, expected %.17g\n",
                     scenario, actual, expected);
        std::exit(1);
    }
}

int main(void) {
    static const double host_values[][5] = {
        {100.0, 104.0, 103.0, 0.0, 0.0},
        {10.0, 20.0, 30.0, 40.0, 50.0},
        {7.0, 0.0, 0.0, 0.0, 0.0},
    };
    static const size_t host_alignment[][5] = {
        {0u, 0u, 1u, 1u, 2u},       /* finalized 1h buckets */
        {0u, 1u, 2u, 3u, 4u},       /* 5m execution series */
        {0u, 0u, 0u, 0u, 0u},       /* finalized 1d bucket */
    };
    static const size_t value_counts[] = {3u, 5u, 1u};
    const double host_input[] = {999.0, 0.0};
    double* values[3] = {};
    size_t* alignment[3] = {};
    double* inputs = nullptr;
    double* outputs = nullptr;
    cxpr_model_tick_state* state = nullptr;
    cxpr_resample_view* views = nullptr;
    cxpr_resample_view host_views[3] = {};

    for (size_t i = 0u; i < 3u; ++i) {
        check(cudaMalloc(&values[i], value_counts[i] * sizeof(double)), "cudaMalloc values");
        check(cudaMalloc(&alignment[i], sizeof(host_alignment[i])), "cudaMalloc alignment");
        check(cudaMemcpy(values[i], host_values[i], value_counts[i] * sizeof(double),
                         cudaMemcpyHostToDevice), "copy values");
        check(cudaMemcpy(alignment[i], host_alignment[i], sizeof(host_alignment[i]),
                         cudaMemcpyHostToDevice), "copy alignment");
    }
    check(cudaMalloc(&inputs, sizeof(host_input)), "cudaMalloc inputs");
    check(cudaMalloc(&outputs, sizeof(double)), "cudaMalloc outputs");
    check(cudaMalloc(&state, sizeof(*state)), "cudaMalloc state");
    check(cudaMalloc(&views, sizeof(host_views)), "cudaMalloc views");
    check(cudaMemcpy(inputs, host_input, sizeof(host_input), cudaMemcpyHostToDevice), "copy inputs");
    check(cudaMemset(state, 0, sizeof(*state)), "clear state");
    for (size_t i = 0u; i < 3u; ++i)
        host_views[i] = {values[i], alignment[i], value_counts[i], 5u};
    check(cudaMemcpy(views, host_views, sizeof(host_views), cudaMemcpyHostToDevice), "copy views");

    run_and_expect(state, inputs, outputs, views, 4u, 471.0,
                   "current/[1]/multiple-interval parity");
    run_and_expect(state, inputs, outputs, views, 0u, NAN, "warmup parity");
    run_and_expect(state, inputs, outputs, views, 3u, 455.0,
                   "finalized-bucket/no-lookahead parity");
    {
        size_t gap_map[5] = {0u, 0u, 1u, 1u, (size_t)-1};
        check(cudaMemcpy(alignment[0], gap_map, sizeof(gap_map), cudaMemcpyHostToDevice),
              "copy gap alignment");
        run_and_expect(state, inputs, outputs, views, 4u, NAN, "gap parity");
        check(cudaMemcpy(alignment[0], host_alignment[0], sizeof(host_alignment[0]),
                         cudaMemcpyHostToDevice), "restore alignment");
    }
    std::puts("CUDA resample current/[1]/warmup/gap/multi-interval parity OK");
    {
        constexpr int iterations = 10000;
        cudaEvent_t begin = nullptr, end = nullptr;
        float elapsed_ms = 0.0f;
        check(cudaEventCreate(&begin), "create benchmark begin event");
        check(cudaEventCreate(&end), "create benchmark end event");
        check(cudaEventRecord(begin), "record transfer begin");
        for (int n = 0; n < iterations; ++n) {
            for (size_t i = 0u; i < 3u; ++i) {
                check(cudaMemcpy(values[i], host_values[i], value_counts[i] * sizeof(double),
                                 cudaMemcpyHostToDevice), "benchmark values transfer");
                check(cudaMemcpy(alignment[i], host_alignment[i], sizeof(host_alignment[i]),
                                 cudaMemcpyHostToDevice), "benchmark alignment transfer");
            }
        }
        check(cudaEventRecord(end), "record transfer end");
        check(cudaEventSynchronize(end), "sync transfer benchmark");
        check(cudaEventElapsedTime(&elapsed_ms, begin, end), "measure transfers");
        std::printf("CUDA resample transfer %.2f ns/three-view-set\n",
                    (double)elapsed_ms * 1.0e6 / iterations);
        check(cudaEventRecord(begin), "record kernel begin");
        for (int n = 0; n < iterations; ++n)
            run_resample<<<1, 1>>>(state, inputs, outputs, views, 4u);
        check(cudaEventRecord(end), "record kernel end");
        check(cudaEventSynchronize(end), "sync kernel benchmark");
        check(cudaEventElapsedTime(&elapsed_ms, begin, end), "measure kernel");
        std::printf("CUDA resample kernel %.2f ns/eval (%d launches)\n",
                    (double)elapsed_ms * 1.0e6 / iterations, iterations);
        const size_t device_bytes = 9u * sizeof(double) + 15u * sizeof(size_t) +
                                    sizeof(host_views) + sizeof(host_input) +
                                    sizeof(double) + sizeof(*state);
        std::printf("CUDA resample fixture device memory %zu bytes\n", device_bytes);
        cudaEventDestroy(end);
        cudaEventDestroy(begin);
    }
    cudaFree(views); cudaFree(state); cudaFree(outputs);
    cudaFree(inputs);
    for (size_t i = 0u; i < 3u; ++i) {
        cudaFree(alignment[i]);
        cudaFree(values[i]);
    }
    return 0;
}
