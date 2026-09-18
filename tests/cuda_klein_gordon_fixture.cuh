#ifndef CXPR_CUDA_KLEIN_GORDON_FIXTURE_CUH
#define CXPR_CUDA_KLEIN_GORDON_FIXTURE_CUH

#include "cxpr_klein_gordon_generated.cuh"

__device__ inline void cxpr_cuda_klein_gordon_cell(
    const double* phi, const double* momentum, const double* source,
    double* next_phi, double* next_momentum, double* acceleration,
    double* energy, size_t count, size_t i, bool periodic) {
    if (!periodic && (i == 0u || i + 1u >= count)) return;
    const size_t left = i == 0u ? count - 1u : i - 1u;
    const size_t right = i + 1u == count ? 0u : i + 1u;
    const cxpr_value inputs[5] = {
        cxpr_num(phi[i]), cxpr_num(momentum[i]), cxpr_num(phi[left]),
        cxpr_num(phi[right]), cxpr_num(source[i])};
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

template <bool Periodic>
__global__ static void cxpr_cuda_klein_gordon_step(
    const double* phi, const double* momentum, const double* source,
    double* next_phi, double* next_momentum, double* acceleration,
    double* energy, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < count)
        cxpr_cuda_klein_gordon_cell(
            phi, momentum, source, next_phi, next_momentum, acceleration,
            energy, count, i, Periodic);
}

#endif
