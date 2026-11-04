/**
 * @file generated_batch.h
 * @brief Ordered view execution for one generated-model session.
 */
#ifndef CXPR_GENERATED_BATCH_H
#define CXPR_GENERATED_BATCH_H

#include <cxpr/generated.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One named rank-one scalar view. */
typedef struct cxpr_generated_view_binding {
    const char* name;
    cxpr_view view;
} cxpr_generated_view_binding;

/** Status returned by generated ordered batch execution. */
typedef enum cxpr_generated_batch_status {
    CXPR_GENERATED_BATCH_OK = 0,
    CXPR_GENERATED_BATCH_INVALID_ARGUMENT,
    CXPR_GENERATED_BATCH_INVALID_DESCRIPTOR,
    CXPR_GENERATED_BATCH_SCHEMA_MISMATCH,
    CXPR_GENERATED_BATCH_INVALID_VIEW
} cxpr_generated_batch_status;

/** Return a stable diagnostic for a generated batch status. */
const char* cxpr_generated_batch_status_message(cxpr_generated_batch_status status);

/**
 * Execute ordered rows against one state block.
 *
 * Views must be rank-one and match descriptor names and scalar types. The
 * function validates the complete binding set before invoking `tick`.
 */
cxpr_generated_batch_status cxpr_generated_tick_batch(
    const cxpr_generated_model_descriptor* descriptor,
    void* state,
    const cxpr_value* params,
    const cxpr_generated_view_binding* inputs,
    size_t input_count,
    cxpr_generated_view_binding* outputs,
    size_t output_count,
    size_t row_count);

#ifdef __cplusplus
}
#endif
#endif
