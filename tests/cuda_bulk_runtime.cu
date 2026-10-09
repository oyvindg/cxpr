#include <cuda_runtime.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "cuda_klein_gordon_fixture.cuh"
#include "cuda_test_helpers.cuh"

using cxpr_cuda_test::device_buffer;

static double host_abs(double value) {
    return value < 0.0 ? -value : value;
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
    device_buffer<double> d_phi(count, "allocate phi");
    device_buffer<double> d_momentum(count, "allocate momentum");
    device_buffer<double> d_source(count, "allocate source");
    device_buffer<double> d_next_phi(count, "allocate next phi");
    device_buffer<double> d_next_momentum(count, "allocate next momentum");
    device_buffer<double> d_acceleration(count, "allocate acceleration");
    device_buffer<double> d_energy(count, "allocate energy");
    for (size_t i = 0u; i < count; ++i) {
        const double x = (double)i * dx;
        phi[i] = std::sin(0.37 * x) + 0.1 * std::cos(0.11 * x);
        momentum[i] = 0.2 * std::cos(0.23 * x);
        source[i] = 0.05 * std::sin(0.07 * x);
    }
    d_phi.upload(phi.data(), count, "copy phi");
    d_momentum.upload(momentum.data(), count, "copy momentum");
    d_source.upload(source.data(), count, "copy source");
    cxpr_cuda_klein_gordon_step<false><<<cxpr_cuda_test::blocks(count), 256>>>(
        d_phi.data(), d_momentum.data(), d_source.data(), d_next_phi.data(),
        d_next_momentum.data(), d_acceleration.data(), d_energy.data(), count);
    cxpr_cuda_test::synchronize("run Klein-Gordon bulk kernel");
    d_next_phi.download(actual_phi.data(), count, "copy next phi");
    d_next_momentum.download(actual_momentum.data(), count, "copy next momentum");
    d_acceleration.download(actual_acceleration.data(), count, "copy acceleration");
    d_energy.download(actual_energy.data(), count, "copy energy");

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
    std::printf("CUDA bulk Klein-Gordon parity OK (%zu cells)\n", count);
    return 0;
}
