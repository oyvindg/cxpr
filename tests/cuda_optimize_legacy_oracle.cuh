#ifndef CXPR_CUDA_OPTIMIZE_LEGACY_ORACLE_CUH
#define CXPR_CUDA_OPTIMIZE_LEGACY_ORACLE_CUH

/* Temporary migration oracle. Remove after sustained GPU/CI parity. */
__global__ static void cxpr_cuda_replay_candidates_legacy(
    cxpr_optimize_candidate_tick_state* states, const cxpr_value* params,
    const cxpr_value* inputs, const unsigned char* active,
    size_t candidate_count, size_t tick_count, cxpr_value* outputs) {
    const size_t candidate = blockIdx.x * blockDim.x + threadIdx.x;
    if (candidate >= candidate_count || active[candidate] == 0u) return;
    for (size_t tick = 0u; tick < tick_count; ++tick)
        cxpr_optimize_candidate_tick(
            &states[candidate], &inputs[tick], &params[candidate * 2u],
            &outputs[candidate * 2u]);
}

#endif
