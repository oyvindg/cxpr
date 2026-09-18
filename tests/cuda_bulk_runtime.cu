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
    const double* phi, const double* momentum, const double* source,
    double* next_phi, double* next_momentum, double* acceleration,
    double* energy, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i == 0u || i + 1u >= count) return;
    const cxpr_value inputs[5] = {
        cxpr_num(phi[i]), cxpr_num(momentum[i]), cxpr_num(phi[i - 1u]),
        cxpr_num(phi[i + 1u]), cxpr_num(source[i])};
    const cxpr_value params[5] = {
        cxpr_num(0.001), cxpr_num(0.01), cxpr_num(1.0),
        cxpr_num(0.1), cxpr_num(0.02)};
    cxpr_value outputs[4];
    cxpr_klein_gordon_tick_state state = {};
    cxpr_klein_gordon_tick(&state, inputs, params, outputs);
    next_phi[i] = outputs[0].d;
    next_momentum[i] = outputs[1].d;
    acceleration[i] = outputs[2].d;
    energy[i] = outputs[3].d;
}

int main(void) {
    constexpr size_t count = 65536u;
    constexpr double dt = 0.001;
    constexpr double dx = 0.01;
    constexpr double mass = 1.0;
    constexpr double lambda = 0.1;
    constexpr double damping = 0.02;
    std::vector<double> phi(count), momentum(count), source(count), actual_phi(count),
        actual_momentum(count), actual_acceleration(count), actual_energy(count);
    double *d_phi = nullptr, *d_momentum = nullptr, *d_source = nullptr,
           *d_next_phi = nullptr, *d_next_momentum = nullptr,
           *d_acceleration = nullptr, *d_energy = nullptr;
    for (size_t i = 0u; i < count; ++i) {
        const double x = (double)i * dx;
        phi[i] = std::sin(0.37 * x) + 0.1 * std::cos(0.11 * x);
        momentum[i] = 0.2 * std::cos(0.23 * x);
        source[i] = 0.05 * std::sin(0.07 * x);
    }
#define ALLOC(p) check(cudaMalloc(&(p), count * sizeof(double)), "cudaMalloc " #p)
    ALLOC(d_phi); ALLOC(d_momentum); ALLOC(d_source); ALLOC(d_next_phi);
    ALLOC(d_next_momentum); ALLOC(d_acceleration); ALLOC(d_energy);
#undef ALLOC
    check(cudaMemcpy(d_phi, phi.data(), count * sizeof(double), cudaMemcpyHostToDevice), "copy phi");
    check(cudaMemcpy(d_momentum, momentum.data(), count * sizeof(double), cudaMemcpyHostToDevice), "copy momentum");
    check(cudaMemcpy(d_source, source.data(), count * sizeof(double), cudaMemcpyHostToDevice), "copy source");
    klein_gordon_bulk<<<(unsigned)((count + 255u) / 256u), 256>>>(
        d_phi, d_momentum, d_source, d_next_phi, d_next_momentum,
        d_acceleration, d_energy, count);
    check(cudaGetLastError(), "launch Klein-Gordon bulk kernel");
    check(cudaDeviceSynchronize(), "synchronize Klein-Gordon bulk kernel");
    check(cudaMemcpy(actual_phi.data(), d_next_phi, count * sizeof(double), cudaMemcpyDeviceToHost), "copy next phi");
    check(cudaMemcpy(actual_momentum.data(), d_next_momentum, count * sizeof(double), cudaMemcpyDeviceToHost), "copy next momentum");
    check(cudaMemcpy(actual_acceleration.data(), d_acceleration, count * sizeof(double), cudaMemcpyDeviceToHost), "copy acceleration");
    check(cudaMemcpy(actual_energy.data(), d_energy, count * sizeof(double), cudaMemcpyDeviceToHost), "copy energy");

    for (size_t i = 1u; i + 1u < count; ++i) {
        const double laplacian = (phi[i - 1u] - 2.0 * phi[i] + phi[i + 1u]) / (dx * dx);
        const double expected_acceleration = laplacian - mass * mass * phi[i]
            - lambda * phi[i] * phi[i] * phi[i]
            - damping * momentum[i] + source[i];
        const double expected_momentum = momentum[i] + dt * expected_acceleration;
        const double expected_phi = phi[i] + dt * expected_momentum;
        const double gradient = (phi[i + 1u] - phi[i - 1u]) / (2.0 * dx);
        const double expected_energy = 0.5 * momentum[i] * momentum[i]
            + 0.5 * gradient * gradient + 0.5 * mass * mass * phi[i] * phi[i]
            + 0.25 * lambda * phi[i] * phi[i] * phi[i] * phi[i];
        const double tolerance = 2e-12;
        if (host_abs(actual_phi[i] - expected_phi) > tolerance ||
            host_abs(actual_momentum[i] - expected_momentum) > tolerance ||
            host_abs(actual_acceleration[i] - expected_acceleration) > tolerance ||
            host_abs(actual_energy[i] - expected_energy) > tolerance) {
            std::fprintf(stderr, "CUDA bulk mismatch at %zu\n", i);
            return 1;
        }
    }
    cudaFree(d_energy); cudaFree(d_acceleration); cudaFree(d_next_momentum);
    cudaFree(d_next_phi); cudaFree(d_source); cudaFree(d_momentum); cudaFree(d_phi);
    std::printf("CUDA bulk Klein-Gordon parity OK (%zu cells)\n", count);
    return 0;
}
