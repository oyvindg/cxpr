#include <cuda_runtime.h>
#include <cxpr/cxpr.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cxpr_optimize_candidate_generated.cuh"

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

__global__ static void replay_candidates(
    cxpr_optimize_candidate_tick_state* states,
    const cxpr_value* params,
    const double* samples,
    size_t candidate_count,
    size_t tick_count,
    double* objectives) {
    const size_t candidate = blockIdx.x * blockDim.x + threadIdx.x;
    if (candidate >= candidate_count) return;
    cxpr_value input[1];
    cxpr_value output[1];
    for (size_t tick = 0u; tick < tick_count; ++tick) {
        input[0] = cxpr_num(samples[tick]);
        cxpr_optimize_candidate_tick(
            &states[candidate], input, &params[candidate], output);
    }
    objectives[candidate] = output[0].d;
}

int main(void) {
    constexpr size_t candidate_count = 4u;
    constexpr size_t tick_count = 5u;
    const double gains[candidate_count] = {1.0, 2.0, 4.0, 8.0};
    const double samples[tick_count] = {1.0, 2.0, -0.5, 3.0, 4.5};
    const double sample_sum = 10.0;
    std::vector<cxpr_value> params(candidate_count);
    std::vector<double> objectives(candidate_count);
    cxpr_optimize_candidate_tick_state* device_states = nullptr;
    cxpr_value* device_params = nullptr;
    double* device_samples = nullptr;
    double* device_objectives = nullptr;

    for (size_t i = 0u; i < candidate_count; ++i) params[i] = cxpr_num(gains[i]);
    check(cudaMalloc(&device_states, candidate_count * sizeof(*device_states)),
          "allocate candidate states");
    check(cudaMalloc(&device_params, candidate_count * sizeof(*device_params)),
          "allocate candidate params");
    check(cudaMalloc(&device_samples, tick_count * sizeof(*device_samples)),
          "allocate samples");
    check(cudaMalloc(&device_objectives, candidate_count * sizeof(*device_objectives)),
          "allocate objectives");
    check(cudaMemset(device_states, 0, candidate_count * sizeof(*device_states)),
          "reset candidate states");
    check(cudaMemcpy(device_params, params.data(), candidate_count * sizeof(*device_params),
                     cudaMemcpyHostToDevice), "copy candidate params");
    check(cudaMemcpy(device_samples, samples, tick_count * sizeof(*device_samples),
                     cudaMemcpyHostToDevice), "copy samples");

    replay_candidates<<<1u, 32u>>>(
        device_states, device_params, device_samples,
        candidate_count, tick_count, device_objectives);
    check(cudaGetLastError(), "launch optimizer candidates");
    check(cudaDeviceSynchronize(), "synchronize optimizer candidates");
    check(cudaMemcpy(objectives.data(), device_objectives,
                     candidate_count * sizeof(*device_objectives),
                     cudaMemcpyDeviceToHost), "copy objectives");

    size_t best = 0u;
    for (size_t i = 0u; i < candidate_count; ++i) {
        const double expected = sample_sum * gains[i];
        if (std::abs(objectives[i] - expected) > 1e-12) {
            std::fprintf(stderr,
                         "CUDA optimizer mismatch candidate=%zu got=%.17g expected=%.17g\n",
                         i, objectives[i], expected);
            return 1;
        }
        if (objectives[i] > objectives[best]) best = i;
    }
    if (best != candidate_count - 1u) return 1;

    cudaFree(device_objectives);
    cudaFree(device_samples);
    cudaFree(device_params);
    cudaFree(device_states);
    std::printf("CUDA optimizer parameter/state isolation parity OK (%zu candidates)\n",
                candidate_count);
    return 0;
}
