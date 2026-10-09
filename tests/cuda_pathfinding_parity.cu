#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cxpr_pathfinding_argmin.cuh"
#include "cxpr_pathfinding_ties.cuh"

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

__global__ static void run_pathfinding_fixtures(double* results) {
    if (blockIdx.x != 0u || threadIdx.x != 0u) return;
    {
        cxpr_value inputs[4] = {};
        cxpr_value outputs[3] = {};
        const double values[4] = {3.0, 1.0, 2.0, 1.0};
        cxpr_pathfinding_argmin_tick_state state = {};
        for (size_t i = 0u; i < 4u; ++i) {
            inputs[i].type = CXPR_VALUE_NUMBER;
            inputs[i].d = values[i];
        }
        cxpr_pathfinding_argmin_tick(&state, inputs, nullptr, outputs);
        results[0] = outputs[0].d;
        results[1] = outputs[1].d;
        results[2] = outputs[2].d;
    }
    {
        cxpr_value inputs[5] = {};
        cxpr_value outputs[3] = {};
        const double values[5] = {
            2.0, 3.0,
            0x1.0000000000001p+0,
            0x1.fffffffffffffp-1,
            -0x1p+0,
        };
        cxpr_pathfinding_ties_tick_state state = {};
        for (size_t i = 0u; i < 5u; ++i) {
            inputs[i].type = CXPR_VALUE_NUMBER;
            inputs[i].d = values[i];
        }
        cxpr_pathfinding_ties_tick(&state, inputs, nullptr, outputs);
        results[3] = outputs[0].d;
        results[4] = outputs[1].d;
        results[5] = outputs[2].d;
    }
}

static std::uint64_t bits(double value) {
    std::uint64_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

int main(void) {
    const double expected[6] = {1.0, 0.0, 7.0, 0.0, 0.0, 0.0};
    double actual[6];
    double* device = nullptr;
    int device_count = 0;
    cudaError_t device_status = cudaGetDeviceCount(&device_count);
    if (device_status == cudaErrorNoDevice || device_count == 0) {
        std::puts("CUDA pathfinding parity SKIP (no CUDA-capable device)");
        return 77;
    }
    check(device_status, "query CUDA devices");
    check(cudaMalloc(&device, sizeof(actual)), "allocate pathfinding result");
    run_pathfinding_fixtures<<<1, 1>>>(device);
    check(cudaGetLastError(), "launch pathfinding parity kernel");
    check(cudaDeviceSynchronize(), "synchronize pathfinding parity kernel");
    check(cudaMemcpy(actual, device, sizeof(actual), cudaMemcpyDeviceToHost),
          "copy pathfinding result");
    cudaFree(device);
    for (size_t i = 0u; i < 6u; ++i) {
        if (bits(actual[i]) != bits(expected[i])) {
            std::fprintf(stderr, "CUDA pathfinding mismatch at %zu: %.17g != %.17g\n",
                         i, actual[i], expected[i]);
            return 1;
        }
    }
    std::puts("CUDA pathfinding fixture bit parity OK");
    return 0;
}
