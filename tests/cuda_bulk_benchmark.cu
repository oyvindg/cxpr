#include <cuda_runtime.h>

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

__global__ static void klein_gordon_step(
    const double* phi, const double* momentum, const double* source,
    double* next_phi, double* next_momentum, double* acceleration,
    double* energy, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const size_t left = i == 0u ? count - 1u : i - 1u;
    const size_t right = i + 1u == count ? 0u : i + 1u;
    const double inputs[5] = {
        phi[i], momentum[i], phi[left], phi[right], source[i]};
    const double params[5] = {0.001, 0.01, 1.0, 0.1, 0.02};
    double outputs[4];
    cxpr_klein_gordon_tick_state state = {};
    cxpr_klein_gordon_tick(&state, inputs, params, outputs);
    next_phi[i] = outputs[0];
    next_momentum[i] = outputs[1];
    acceleration[i] = outputs[2];
    energy[i] = outputs[3];
}

static size_t parse_size(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (!text[0] || !end || *end || value == 0u) {
        std::fprintf(stderr, "%s must be a positive integer\n", name);
        std::exit(2);
    }
    return (size_t)value;
}

int main(int argc, char** argv) {
    const size_t count = argc > 1 ? parse_size(argv[1], "cells") : 1048576u;
    const size_t steps = argc > 2 ? parse_size(argv[2], "steps") : 1000u;
    const unsigned block_size =
        (unsigned)(argc > 3 ? parse_size(argv[3], "block size") : 256u);
    const size_t repetitions =
        argc > 4 ? parse_size(argv[4], "repetitions") : 7u;
    constexpr double dt = 0.001;
    constexpr double dx = 0.01;
    constexpr double mass = 1.0;
    constexpr double lambda = 0.1;
    constexpr double damping = 0.02;
    constexpr size_t warmup_steps = 10u;
    if (count < 3u || block_size > 1024u) {
        std::fprintf(stderr, "cells must be >= 3 and block size <= 1024\n");
        return 2;
    }

    std::vector<double> initial_phi(count), initial_momentum(count), source(count);
    std::vector<double> actual_phi(count), actual_momentum(count);
    std::vector<double> reference_phi(count), reference_momentum(count);
    std::vector<double> reference_next_phi(count), reference_next_momentum(count);
    double *d_phi_a = nullptr, *d_phi_b = nullptr, *d_momentum_a = nullptr,
           *d_momentum_b = nullptr, *d_source = nullptr,
           *d_acceleration = nullptr, *d_energy = nullptr;
    cudaEvent_t start = nullptr, stop = nullptr;
    cudaDeviceProp device = {};
    const size_t bytes = count * sizeof(double);
    const unsigned grid_size = (unsigned)((count + block_size - 1u) / block_size);

    for (size_t i = 0u; i < count; ++i) {
        const double x = (double)i * dx;
        initial_phi[i] = std::sin(0.37 * x) + 0.1 * std::cos(0.11 * x);
        initial_momentum[i] = 0.2 * std::cos(0.23 * x);
        source[i] = 0.05 * std::sin(0.07 * x);
    }
#define ALLOC(p) check(cudaMalloc(&(p), bytes), "cudaMalloc " #p)
    ALLOC(d_phi_a); ALLOC(d_phi_b); ALLOC(d_momentum_a); ALLOC(d_momentum_b);
    ALLOC(d_source); ALLOC(d_acceleration); ALLOC(d_energy);
#undef ALLOC
    check(cudaMemcpy(d_source, source.data(), bytes, cudaMemcpyHostToDevice), "copy source");
    check(cudaEventCreate(&start), "create start event");
    check(cudaEventCreate(&stop), "create stop event");
    check(cudaGetDeviceProperties(&device, 0), "query CUDA device");

    auto reset_fields = [&]() {
        check(cudaMemcpy(d_phi_a, initial_phi.data(), bytes, cudaMemcpyHostToDevice), "reset phi");
        check(cudaMemcpy(d_momentum_a, initial_momentum.data(), bytes, cudaMemcpyHostToDevice), "reset momentum");
    };
    auto launch_steps = [&](size_t step_count, double*& phi, double*& next_phi,
                            double*& momentum, double*& next_momentum) {
        for (size_t step = 0u; step < step_count; ++step) {
            klein_gordon_step<<<grid_size, block_size>>>(
                phi, momentum, d_source, next_phi, next_momentum,
                d_acceleration, d_energy, count);
            double* swap = phi; phi = next_phi; next_phi = swap;
            swap = momentum; momentum = next_momentum; next_momentum = swap;
        }
    };

    reset_fields();
    double *phi = d_phi_a, *next_phi = d_phi_b;
    double *momentum = d_momentum_a, *next_momentum = d_momentum_b;
    launch_steps(warmup_steps, phi, next_phi, momentum, next_momentum);
    check(cudaDeviceSynchronize(), "warmup");

    std::vector<float> timings(repetitions);
    for (size_t repetition = 0u; repetition < repetitions; ++repetition) {
        reset_fields();
        phi = d_phi_a; next_phi = d_phi_b;
        momentum = d_momentum_a; next_momentum = d_momentum_b;
        check(cudaEventRecord(start), "record start");
        launch_steps(steps, phi, next_phi, momentum, next_momentum);
        check(cudaGetLastError(), "launch resident steps");
        check(cudaEventRecord(stop), "record stop");
        check(cudaEventSynchronize(stop), "wait for benchmark");
        float elapsed_ms = 0.0f;
        check(cudaEventElapsedTime(&elapsed_ms, start, stop), "measure benchmark");
        timings[repetition] = elapsed_ms;
    }
    check(cudaMemcpy(actual_phi.data(), phi, bytes, cudaMemcpyDeviceToHost), "copy final phi");
    check(cudaMemcpy(actual_momentum.data(), momentum, bytes, cudaMemcpyDeviceToHost), "copy final momentum");

    reference_phi = initial_phi;
    reference_momentum = initial_momentum;
    for (size_t step = 0u; step < steps; ++step) {
        for (size_t i = 0u; i < count; ++i) {
            const size_t left = i == 0u ? count - 1u : i - 1u;
            const size_t right = i + 1u == count ? 0u : i + 1u;
            const double laplacian =
                (reference_phi[left] - 2.0 * reference_phi[i] + reference_phi[right]) / (dx * dx);
            const double acceleration = laplacian - mass * mass * reference_phi[i]
                - lambda * reference_phi[i] * reference_phi[i] * reference_phi[i]
                - damping * reference_momentum[i] + source[i];
            reference_next_momentum[i] = reference_momentum[i] + dt * acceleration;
            reference_next_phi[i] = reference_phi[i] + dt * reference_next_momentum[i];
        }
        reference_phi.swap(reference_next_phi);
        reference_momentum.swap(reference_next_momentum);
    }
    for (size_t i = 0u; i < count; ++i) {
        const double scale_phi = 1.0 + host_abs(reference_phi[i]);
        const double scale_momentum = 1.0 + host_abs(reference_momentum[i]);
        if (host_abs(actual_phi[i] - reference_phi[i]) > 2e-11 * scale_phi ||
            host_abs(actual_momentum[i] - reference_momentum[i]) > 2e-11 * scale_momentum) {
            std::fprintf(stderr, "resident CUDA parity mismatch at %zu\n", i);
            return 1;
        }
    }

    for (size_t i = 1u; i < repetitions; ++i) {
        const float value = timings[i];
        size_t position = i;
        while (position > 0u && timings[position - 1u] > value) {
            timings[position] = timings[position - 1u];
            --position;
        }
        timings[position] = value;
    }
    const double median_ms = repetitions % 2u
        ? timings[repetitions / 2u]
        : 0.5 * ((double)timings[repetitions / 2u - 1u]
                 + (double)timings[repetitions / 2u]);
    const double updates = (double)count * (double)steps;
    const double seconds = median_ms / 1000.0;
    const double updates_per_second = updates / seconds;
    const double effective_gb_per_second = updates_per_second * 72.0 / 1.0e9;
    std::printf(
        "GPU: %s, compute capability %d.%d\n"
        "CXPR CUDA resident benchmark: %zu cells x %zu steps, block=%u, repetitions=%zu\n"
        "kernel time ms: median=%.3f, min=%.3f, max=%.3f\n"
        "throughput: %.3f Gcell-steps/s, %.2f effective GB/s\n"
        "multi-step CPU parity: OK\n",
        device.name, device.major, device.minor, count, steps, block_size,
        repetitions, median_ms, (double)timings.front(), (double)timings.back(),
        updates_per_second / 1.0e9,
        effective_gb_per_second);

    cudaEventDestroy(stop); cudaEventDestroy(start);
    cudaFree(d_energy); cudaFree(d_acceleration); cudaFree(d_source);
    cudaFree(d_momentum_b); cudaFree(d_momentum_a);
    cudaFree(d_phi_b); cudaFree(d_phi_a);
    return 0;
}
