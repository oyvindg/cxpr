/**
 * @file view.h
 * @brief Validation and resolved bindings for model-visible external views.
 */

#ifndef CXPR_VIEW_H
#define CXPR_VIEW_H

#include <stddef.h>
#include <stdint.h>
#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_VIEW_ABI_VERSION 1u
#define CXPR_VIEW_MAX_COUNT 32u

typedef enum cxpr_view_access {
    CXPR_VIEW_ACCESS_READ_ONLY = 1
} cxpr_view_access;

/** Build-time contract for one model-visible, host-owned view. */
typedef struct cxpr_view_requirement {
    const char* name;
    size_t slot;
    cxpr_value_type element_type;
    size_t rank;
    size_t max_shape[CXPR_VIEW_MAX_RANK];
    size_t min_alignment;
    cxpr_view_access access;
} cxpr_view_requirement;

/** Binding prepared once and consumed without schema lookup during ticks. */
typedef struct cxpr_view_resolved {
    const void* data;
    cxpr_value_type element_type;
    size_t rank;
    size_t shape[CXPR_VIEW_MAX_RANK];
    size_t element_strides[CXPR_VIEW_MAX_RANK];
} cxpr_view_resolved;

/** Ordered binding table passed to a generated view-aware tick. */
typedef struct cxpr_view_table {
    const cxpr_view_resolved* views;
    size_t count;
    uint64_t generation;
} cxpr_view_table;

typedef enum cxpr_view_status {
    CXPR_VIEW_OK = 0,
    CXPR_VIEW_NULL_ARGUMENT,
    CXPR_VIEW_INVALID_REQUIREMENT,
    CXPR_VIEW_MISSING_BINDING,
    CXPR_VIEW_TYPE_MISMATCH,
    CXPR_VIEW_RANK_MISMATCH,
    CXPR_VIEW_SHAPE_EXCEEDED,
    CXPR_VIEW_INVALID_FLAGS,
    CXPR_VIEW_NON_CONTIGUOUS,
    CXPR_VIEW_CAPACITY_EXCEEDED,
    CXPR_VIEW_MISALIGNED,
    CXPR_VIEW_OVERFLOW,
    CXPR_VIEW_STALE_BINDING,
    CXPR_VIEW_INVALID_INDEX,
    CXPR_VIEW_INDEX_OUT_OF_RANGE
} cxpr_view_status;

/** Return a stable host-facing message for a validation status. */
const char* cxpr_view_status_message(cxpr_view_status status);

/**
 * Validate and resolve ordered host bindings before execution.
 *
 * @param requirements Ordered generated-model requirements.
 * @param bindings Ordered host bindings matching `requirements` by slot.
 * @param count Number of requirements, bindings, and output entries.
 * @param resolved Caller-owned output storage with at least `count` entries.
 * @param failed_slot Optional output receiving the failing requirement slot.
 * @returns A detailed status. On failure, no resolved table may be executed.
 */
cxpr_view_status cxpr_views_resolve(
    const cxpr_view_requirement* requirements,
    const cxpr_view* bindings,
    size_t count,
    cxpr_view_resolved* resolved,
    size_t* failed_slot);

/**
 * Read one active dimension from a prevalidated view table.
 * @param table Resolved table for the current execution lease.
 * @param slot Compile-resolved view slot.
 * @param dimension Compile-resolved dimension.
 * @param out_length Receives the active dimension length.
 * @returns `CXPR_VIEW_OK` or a bounded lookup error.
 */
cxpr_view_status cxpr_view_length(
    const cxpr_view_table* table,
    size_t slot,
    size_t dimension,
    size_t* out_length);

/**
 * Perform a checked typed load. Indices must be finite integral numbers.
 * @param table Resolved table for the current execution lease.
 * @param slot Compile-resolved view slot.
 * @param indices One index per declared view dimension.
 * @param index_count Number of supplied indices; must equal the view rank.
 * @param out_value Receives a number, int64, or bool value on success.
 * @returns A bounded status; native memory is never read on invalid access.
 */
cxpr_view_status cxpr_view_load_checked(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value* out_value);

/**
 * Perform a checked load and use a typed fallback for index failures.
 * @returns `CXPR_VIEW_OK` when loaded, otherwise the original status.
 */
cxpr_view_status cxpr_view_load_or(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value fallback,
    cxpr_value* out_value);

/**
 * Clamp integral indices to active bounds before loading.
 * @returns An error for fractional/non-finite indices or empty dimensions.
 */
cxpr_view_status cxpr_view_load_clamped(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value* out_value);

#ifdef __cplusplus
}
#endif

#endif
