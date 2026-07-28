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

/** @brief ABI version implemented by generated model descriptors. */
#define CXPR_GENERATED_MODEL_ABI_VERSION 3u
/** @brief Maximum number of inputs represented by a descriptor. */
#define CXPR_GENERATED_MODEL_MAX_INPUTS 64u
/** @brief Maximum number of outputs represented by a descriptor. */
#define CXPR_GENERATED_MODEL_MAX_OUTPUTS 64u
/** @brief Maximum number of parameters represented by a descriptor. */
#define CXPR_GENERATED_MODEL_MAX_PARAMS 64u

/** @brief Evaluate one generated-model tick. */
typedef void (*cxpr_generated_tick_fn)(
    void* state,
    const double* inputs,
    const double* params,
    double* outputs);

/** @brief Return the state-block size required by a generated model. */
typedef size_t (*cxpr_generated_state_size_fn)(void);
/** @brief Reset a generated model state block. */
typedef void (*cxpr_generated_reset_fn)(void* state);

/**
 * Build-time descriptor for one generated model.
 *
 * Names and callbacks have static artifact lifetime. The descriptor contains no
 * host concepts; hosts decide how inputs are sourced and outputs are consumed.
 * Before the first tick, hosts must zero the full state block returned by
 * `state_size`, or call `reset` when the descriptor provides one.
 */
typedef struct cxpr_generated_model_descriptor {
    const char* name;                 /**< Stable model name. */
    cxpr_generated_tick_fn tick;      /**< Evaluator producing all outputs. */
    /** Output-specific evaluators, indexed like @ref output_names. */
    cxpr_generated_tick_fn selected_ticks[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    cxpr_generated_state_size_fn state_size; /**< Full evaluator state size. */
    /** State sizes required by the output-specific evaluators. */
    cxpr_generated_state_size_fn
        selected_state_sizes[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t param_count; /**< Number of entries in the parameter arrays. */
    /** Ordered input names consumed by @ref tick. */
    const char* input_names[CXPR_GENERATED_MODEL_MAX_INPUTS];
    size_t input_count; /**< Number of entries in @ref input_names. */
    /** Ordered output names produced by @ref tick. */
    const char* output_names[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t output_count; /**< Number of entries in @ref output_names. */
    uint32_t abi_version; /**< Descriptor ABI version. */
    cxpr_generated_reset_fn reset; /**< Optional state reset callback. */
    /** Ordered parameter names consumed by @ref tick. */
    const char* param_names[CXPR_GENERATED_MODEL_MAX_PARAMS];
    /** Default value for each parameter that has a default. */
    double param_defaults[CXPR_GENERATED_MODEL_MAX_PARAMS];
    /** Non-zero for parameters with an entry in @ref param_defaults. */
    unsigned char param_has_default[CXPR_GENERATED_MODEL_MAX_PARAMS];
} cxpr_generated_model_descriptor;

/**
 * @brief Validate the ABI and bounds of a generated model descriptor.
 * @param descriptor Descriptor to validate.
 * @return Non-zero when the descriptor can be consumed by this ABI.
 */
static inline int cxpr_generated_model_descriptor_abi_valid(
    const cxpr_generated_model_descriptor* descriptor) {
    return descriptor &&
           descriptor->abi_version == CXPR_GENERATED_MODEL_ABI_VERSION &&
           descriptor->name &&
           descriptor->tick &&
           descriptor->input_count <= CXPR_GENERATED_MODEL_MAX_INPUTS &&
           descriptor->output_count <= CXPR_GENERATED_MODEL_MAX_OUTPUTS &&
           descriptor->param_count <= CXPR_GENERATED_MODEL_MAX_PARAMS;
}

#ifdef __cplusplus
}
#endif

#endif
