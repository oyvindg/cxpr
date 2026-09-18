#ifndef CXPR_CUDA_ROLLING_BUFFER_FIXTURE_CUH
#define CXPR_CUDA_ROLLING_BUFFER_FIXTURE_CUH

#include "cxpr_rolling_buffer_generated.cuh"

__global__ static void cxpr_cuda_rolling_buffer_step(
    cxpr_rolling_buffer_tick_state* states, const double* inputs,
    double* outputs, size_t count) {
    const size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= count) return;
    const cxpr_value element_inputs[1] = {cxpr_num(inputs[i])};
    cxpr_value element_outputs[3];
    cxpr_rolling_buffer_tick(&states[i], element_inputs, nullptr, element_outputs);
    for (size_t output = 0u; output < 3u; ++output)
        outputs[output * count + i] = element_outputs[output].d;
}

#endif
