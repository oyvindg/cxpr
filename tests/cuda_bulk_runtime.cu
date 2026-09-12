#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cxpr_klein_gordon_generated.cuh"

static void check(cudaError_t status, const char* operation) {
    if (status == cudaSuccess) return;
    std::fprintf(stderr, "%s: %s\n", operation, cudaGetErrorString(status));
    std::exit(2);
}

static double host_abs(double value) {
    return value < 0.0 ? -value : value;
}

__global__ static void klein_gordon_bulk(
    const double* phi, const double* momentum,
    double* next_phi, double* next_momentum, double* energy, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i == 0u || i + 1u >= count) return;
    const double inputs[4] = {phi[i], momentum[i], phi[i - 1u], phi[i + 1u]};
    const double params[3] = {0.001, 0.01, 1.0};
    double outputs[3];
    cxpr_klein_gordon_tick_state state = {};
    cxpr_klein_gordon_tick(&state, inputs, params, outputs);
    next_phi[i] = outputs[0];
    next_momentum[i] = outputs[1];
    energy[i] = outputs[2];
}

int main(void) {
    constexpr size_t count = 65536u;
    constexpr double dt = 0.001;
    constexpr double dx = 0.01;
    constexpr double mass = 1.0;
    std::vector<double> phi(count), momentum(count), actual_phi(count),
        actual_momentum(count), actual_energy(count);
    double *d_phi = nullptr, *d_momentum = nullptr, *d_next_phi = nullptr,
           *d_next_momentum = nullptr, *d_energy = nullptr;
    for (size_t i = 0u; i < count; ++i) {
        const double x = (double)i * dx;
        phi[i] = std::sin(0.37 * x) + 0.1 * std::cos(0.11 * x);
        momentum[i] = 0.2 * std::cos(0.23 * x);
    }
#define ALLOC(p) check(cudaMalloc(&(p), count * sizeof(double)), "cudaMalloc " #p)
    ALLOC(d_phi); ALLOC(d_momentum); ALLOC(d_next_phi); ALLOC(d_next_momentum); ALLOC(d_energy);
#undef ALLOC
    check(cudaMemcpy(d_phi, phi.data(), count * sizeof(double), cudaMemcpyHostToDevice), "copy phi");
    check(cudaMemcpy(d_momentum, momentum.data(), count * sizeof(double), cudaMemcpyHostToDevice), "copy momentum");
    klein_gordon_bulk<<<(unsigned)((count + 255u) / 256u), 256>>>(
        d_phi, d_momentum, d_next_phi, d_next_momentum, d_energy, count);
    check(cudaGetLastError(), "launch Klein-Gordon bulk kernel");
    check(cudaDeviceSynchronize(), "synchronize Klein-Gordon bulk kernel");
    check(cudaMemcpy(actual_phi.data(), d_next_phi, count * sizeof(double), cudaMemcpyDeviceToHost), "copy next phi");
    check(cudaMemcpy(actual_momentum.data(), d_next_momentum, count * sizeof(double), cudaMemcpyDeviceToHost), "copy next momentum");
    check(cudaMemcpy(actual_energy.data(), d_energy, count * sizeof(double), cudaMemcpyDeviceToHost), "copy energy");

    for (size_t i = 1u; i + 1u < count; ++i) {
        const double laplacian = (phi[i - 1u] - 2.0 * phi[i] + phi[i + 1u]) / (dx * dx);
        const double expected_momentum = momentum[i] + dt * (laplacian - mass * mass * phi[i]);
        const double expected_phi = phi[i] + dt * expected_momentum;
        const double expected_energy = 0.5 * expected_momentum * expected_momentum
            + 0.5 * mass * mass * expected_phi * expected_phi;
        const double tolerance = 2e-12;
        if (host_abs(actual_phi[i] - expected_phi) > tolerance ||
            host_abs(actual_momentum[i] - expected_momentum) > tolerance ||
            host_abs(actual_energy[i] - expected_energy) > tolerance) {
            std::fprintf(stderr, "CUDA bulk mismatch at %zu\n", i);
            return 1;
        }
    }
    cudaFree(d_energy); cudaFree(d_next_momentum); cudaFree(d_next_phi);
    cudaFree(d_momentum); cudaFree(d_phi);
    std::printf("CUDA bulk Klein-Gordon parity OK (%zu cells)\n", count);
    return 0;
}
