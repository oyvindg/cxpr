/**
 * @file generated.h
 * @brief Stable, host-neutral ABI for build-time generated model evaluators.
 */

#ifndef CXPR_GENERATED_H
#define CXPR_GENERATED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_GENERATED_MODEL_ABI_VERSION 2u
#define CXPR_GENERATED_MODEL_MAX_INPUTS 8u
#define CXPR_GENERATED_MODEL_MAX_OUTPUTS 12u

typedef void (*cxpr_generated_tick_fn)(
    void* state,
    const double* inputs,
    const double* params,
    double* outputs);

typedef size_t (*cxpr_generated_state_size_fn)(void);
typedef void (*cxpr_generated_reset_fn)(void* state);

/**
 * Build-time descriptor for one generated model.
 *
 * Names and callbacks have static artifact lifetime. The descriptor contains no
 * host concepts; hosts decide how inputs are sourced and outputs are consumed.
 */
typedef struct cxpr_generated_model_descriptor {
    const char* name;
    cxpr_generated_tick_fn tick;
    cxpr_generated_tick_fn selected_ticks[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    cxpr_generated_state_size_fn state_size;
    cxpr_generated_state_size_fn
        selected_state_sizes[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t param_count;
    const char* input_names[CXPR_GENERATED_MODEL_MAX_INPUTS];
    size_t input_count;
    const char* output_names[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t output_count;
    uint32_t abi_version;
    cxpr_generated_reset_fn reset;
    const char* param_names[CXPR_GENERATED_MODEL_MAX_INPUTS];
    double param_defaults[CXPR_GENERATED_MODEL_MAX_INPUTS];
    unsigned char param_has_default[CXPR_GENERATED_MODEL_MAX_INPUTS];
} cxpr_generated_model_descriptor;

static inline int cxpr_generated_model_descriptor_abi_valid(
    const cxpr_generated_model_descriptor* descriptor) {
    return descriptor &&
           descriptor->abi_version == CXPR_GENERATED_MODEL_ABI_VERSION &&
           descriptor->name &&
           descriptor->tick &&
           descriptor->input_count <= CXPR_GENERATED_MODEL_MAX_INPUTS &&
           descriptor->output_count <= CXPR_GENERATED_MODEL_MAX_OUTPUTS;
}

#ifdef __cplusplus
}
#endif

#endif
