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
#define CXPR_GENERATED_MODEL_ABI_VERSION 4u
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

/** Pre-materialized temporal series consumed by generated resample models. */
#ifndef CXPR_RESAMPLE_VIEW_ABI_VERSION
#define CXPR_RESAMPLE_VIEW_ABI_VERSION 1u
#endif
#ifndef CXPR_RESAMPLE_VIEW_VALUE_TYPE
#define CXPR_RESAMPLE_VIEW_VALUE_TYPE CXPR_GENERATED_VALUE_NUMBER
#endif
#ifndef CXPR_RESAMPLE_ALIGNMENT_MISSING
#define CXPR_RESAMPLE_ALIGNMENT_MISSING ((size_t)-1)
#endif
#ifndef CXPR_RESAMPLE_VIEW_DEFINED
#define CXPR_RESAMPLE_VIEW_DEFINED 1
typedef struct cxpr_resample_view {
    const double* values;
    const size_t* alignment;
    size_t value_count;
    size_t primary_count;
} cxpr_resample_view;
#endif

typedef enum cxpr_resample_view_status {
    CXPR_RESAMPLE_VIEW_OK = 0,
    CXPR_RESAMPLE_VIEW_NULL,
    CXPR_RESAMPLE_VIEW_VALUES_REQUIRED,
    CXPR_RESAMPLE_VIEW_ALIGNMENT_REQUIRED
} cxpr_resample_view_status;

/** Return a stable diagnostic string suitable for host error reporting. */
static inline const char* cxpr_resample_view_status_message(
    cxpr_resample_view_status status) {
    switch (status) {
    case CXPR_RESAMPLE_VIEW_OK: return "resample view is valid";
    case CXPR_RESAMPLE_VIEW_NULL: return "resample view is NULL";
    case CXPR_RESAMPLE_VIEW_VALUES_REQUIRED:
        return "numeric resample view requires a values buffer";
    case CXPR_RESAMPLE_VIEW_ALIGNMENT_REQUIRED:
        return "resample view with primary rows requires an alignment map";
    default: return "unknown resample view error";
    }
}

static inline cxpr_resample_view_status cxpr_resample_view_validate(
    const cxpr_resample_view* view) {
    if (!view) return CXPR_RESAMPLE_VIEW_NULL;
    if (!view->values) return CXPR_RESAMPLE_VIEW_VALUES_REQUIRED;
    if (view->primary_count > 0u && !view->alignment)
        return CXPR_RESAMPLE_VIEW_ALIGNMENT_REQUIRED;
    return CXPR_RESAMPLE_VIEW_OK;
}

/** Validate one numeric resample view before a host uploads or evaluates it. */
static inline int cxpr_resample_view_valid(const cxpr_resample_view* view) {
    return cxpr_resample_view_validate(view) == CXPR_RESAMPLE_VIEW_OK;
}

/** @brief Evaluate one generated-model tick with pre-bound temporal series. */
typedef void (*cxpr_generated_resample_tick_fn)(
    void* state,
    const double* inputs,
    const double* params,
    double* outputs,
    const cxpr_resample_view* views,
    size_t primary_cursor);

/** @brief Return the state-block size required by a generated model. */
typedef size_t (*cxpr_generated_state_size_fn)(void);
/** @brief Reset a generated model state block. */
typedef void (*cxpr_generated_reset_fn)(void* state);

/** Scalar value types transported by the generated model ABI. */
typedef enum cxpr_generated_value_type {
    CXPR_GENERATED_VALUE_UNKNOWN = 0,
    CXPR_GENERATED_VALUE_NUMBER = 1,
    CXPR_GENERATED_VALUE_BOOL = 2
} cxpr_generated_value_type;

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
    /** Scalar type for each ordered input. */
    cxpr_generated_value_type input_types[CXPR_GENERATED_MODEL_MAX_INPUTS];
    size_t input_count; /**< Number of entries in @ref input_names. */
    /** Ordered output names produced by @ref tick. */
    const char* output_names[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    /** Scalar type for each ordered output. */
    cxpr_generated_value_type output_types[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t output_count; /**< Number of entries in @ref output_names. */
    uint32_t abi_version; /**< Descriptor ABI version. */
    cxpr_generated_reset_fn reset; /**< Optional state reset callback. */
    /** Ordered parameter names consumed by @ref tick. */
    const char* param_names[CXPR_GENERATED_MODEL_MAX_PARAMS];
    /** Scalar type for each ordered parameter. */
    cxpr_generated_value_type param_types[CXPR_GENERATED_MODEL_MAX_PARAMS];
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
    size_t i;
    if (!descriptor ||
        descriptor->abi_version != CXPR_GENERATED_MODEL_ABI_VERSION ||
        !descriptor->name || !descriptor->tick || !descriptor->state_size ||
        descriptor->input_count > CXPR_GENERATED_MODEL_MAX_INPUTS ||
        descriptor->output_count > CXPR_GENERATED_MODEL_MAX_OUTPUTS ||
        descriptor->param_count > CXPR_GENERATED_MODEL_MAX_PARAMS) {
        return 0;
    }
    for (i = 0u; i < descriptor->input_count; ++i) {
        if (!descriptor->input_names[i] ||
            (descriptor->input_types[i] != CXPR_GENERATED_VALUE_NUMBER &&
             descriptor->input_types[i] != CXPR_GENERATED_VALUE_BOOL)) {
            return 0;
        }
    }
    for (i = 0u; i < descriptor->output_count; ++i) {
        if (!descriptor->output_names[i] ||
            (descriptor->output_types[i] != CXPR_GENERATED_VALUE_NUMBER &&
             descriptor->output_types[i] != CXPR_GENERATED_VALUE_BOOL)) {
            return 0;
        }
    }
    for (i = 0u; i < descriptor->param_count; ++i) {
        if (!descriptor->param_names[i] ||
            (descriptor->param_types[i] != CXPR_GENERATED_VALUE_NUMBER &&
             descriptor->param_types[i] != CXPR_GENERATED_VALUE_BOOL)) {
            return 0;
        }
    }
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
