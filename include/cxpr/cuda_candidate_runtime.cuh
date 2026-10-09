#ifndef CXPR_CUDA_CANDIDATE_RUNTIME_CUH
#define CXPR_CUDA_CANDIDATE_RUNTIME_CUH

#include <cxpr/types.h>
#include <stddef.h>

/*
 * Defines a one-thread-per-candidate replay kernel around generated CXPR tick
 * code. Inputs are shared tick-major rows. Params, states, and final outputs
 * are candidate-major. `active` may be NULL; inactive output rows are left
 * untouched so hosts can prefill a sentinel and verify constraint filtering.
 */
#define CXPR_CUDA_DEFINE_CANDIDATE_REPLAY_KERNEL(                              \
    kernel_name, state_type, tick_function)                                    \
    __global__ static void kernel_name(                                        \
        state_type* states, const cxpr_value* params, size_t param_stride,      \
        const cxpr_value* inputs, size_t input_count, size_t tick_count,        \
        const unsigned char* active, cxpr_value* final_outputs,                 \
        size_t output_count, size_t candidate_offset, size_t candidate_count) { \
        const size_t local = blockIdx.x * blockDim.x + threadIdx.x;             \
        if (local >= candidate_count) return;                                   \
        const size_t candidate = candidate_offset + local;                      \
        if (active && active[candidate] == 0u) return;                          \
        cxpr_value* output_row = final_outputs + candidate * output_count;       \
        const cxpr_value* param_row = params + candidate * param_stride;         \
        for (size_t tick = 0u; tick < tick_count; ++tick)                       \
            tick_function(&states[candidate], inputs + tick * input_count,      \
                          param_row, output_row);                               \
    }

/* Runtime-compiled variant that keeps candidate state local to each thread.
 * This avoids exposing generated state sizes to a generic host adapter. */
#define CXPR_CUDA_DEFINE_RESIDENT_CANDIDATE_REPLAY_KERNEL(                     \
    kernel_name, state_type, tick_function, output_count_value)                \
    extern "C" __global__ void kernel_name(                                  \
        size_t* state_size_output, void* state_storage,                         \
        const cxpr_value* params, const cxpr_value* inputs,                     \
        const unsigned char* active, cxpr_value* outputs,                       \
        size_t candidate_count, size_t param_count,                             \
        size_t input_count, size_t tick_count) {                                \
        const size_t candidate = blockIdx.x * blockDim.x + threadIdx.x;         \
        if (candidate == 0u && state_size_output)                               \
            *state_size_output = sizeof(state_type);                            \
        if (!state_storage) return;                                             \
        if (candidate >= candidate_count || !active[candidate]) return;          \
        state_type* state = ((state_type*)state_storage) + candidate;           \
        unsigned char* state_bytes = (unsigned char*)state;                     \
        for (size_t byte = 0u; byte < sizeof(state_type); ++byte)               \
            state_bytes[byte] = 0u;                                             \
        cxpr_value row[output_count_value];                                     \
        for (size_t tick = 0u; tick < tick_count; ++tick)                       \
            tick_function(state, inputs + tick * input_count,                   \
                          params + candidate * param_count, row);               \
        for (size_t output = 0u; output < output_count_value; ++output)         \
            outputs[candidate * output_count_value + output] = row[output];     \
    }

#endif
