/**
 * @file bulk.h
 * @brief Host-neutral bulk execution for generated scalar models.
 *
 * The host owns topology, storage, scheduling, and boundary conditions. cxpr
 * only maps one generated model evaluation to each logical element.
 */

#ifndef CXPR_BULK_H
#define CXPR_BULK_H

#include <cxpr/generated.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One strided scalar column. A zero stride broadcasts element zero. */
typedef struct cxpr_bulk_const_column {
    const double* values;
    size_t stride;
} cxpr_bulk_const_column;

/** One writable strided scalar column. */
typedef struct cxpr_bulk_column {
    double* values;
    size_t stride;
} cxpr_bulk_column;

/**
 * Host-owned buffers for a range of independent generated-model evaluations.
 *
 * Inputs and outputs use structure-of-arrays layout. `states` contains one
 * model state block per element; `state_stride` is measured in bytes. Params
 * are shared by the whole launch. Hosts materialize neighbors, coordinates,
 * tensors, or complex components as ordinary named scalar input columns.
 */
typedef struct cxpr_bulk_view {
    const cxpr_bulk_const_column* inputs;
    size_t input_count;
    const double* params;
    size_t param_count;
    cxpr_bulk_column* outputs;
    size_t output_count;
    void* states;
    size_t state_stride;
    size_t element_count;
} cxpr_bulk_view;

typedef enum cxpr_bulk_status {
    CXPR_BULK_OK = 0,
    CXPR_BULK_INVALID_ARGUMENT,
    CXPR_BULK_INVALID_DESCRIPTOR,
    CXPR_BULK_SCHEMA_MISMATCH,
    CXPR_BULK_MISSING_BUFFER,
    CXPR_BULK_STATE_STRIDE_TOO_SMALL,
    CXPR_BULK_RANGE_OUT_OF_BOUNDS
} cxpr_bulk_status;

/** Return a stable diagnostic string for a bulk status. */
const char* cxpr_bulk_status_message(cxpr_bulk_status status);

/** Validate a descriptor/view pair without executing it. */
cxpr_bulk_status cxpr_bulk_validate(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view);

/** Execute `[begin, begin + count)` serially. Disjoint ranges may run in parallel. */
cxpr_bulk_status cxpr_bulk_run_range(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    size_t begin,
    size_t count);

/** Execute all elements serially. */
cxpr_bulk_status cxpr_bulk_run(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_BULK_H */
