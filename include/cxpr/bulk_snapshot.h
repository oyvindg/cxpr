/**
 * @file bulk_snapshot.h
 * @brief Versioned persistence for generated-model bulk state.
 */

#ifndef CXPR_BULK_SNAPSHOT_H
#define CXPR_BULK_SNAPSHOT_H

#include <cxpr/bulk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_BULK_SNAPSHOT_FORMAT_VERSION 1u

typedef struct cxpr_bulk_snapshot {
    unsigned char* data;
    size_t size;
} cxpr_bulk_snapshot;

typedef enum cxpr_bulk_snapshot_status {
    CXPR_BULK_SNAPSHOT_OK = 0,
    CXPR_BULK_SNAPSHOT_INVALID_ARGUMENT,
    CXPR_BULK_SNAPSHOT_INVALID_DESCRIPTOR,
    CXPR_BULK_SNAPSHOT_INVALID_FORMAT,
    CXPR_BULK_SNAPSHOT_UNSUPPORTED_VERSION,
    CXPR_BULK_SNAPSHOT_LAYOUT_MISMATCH,
    CXPR_BULK_SNAPSHOT_ELEMENT_COUNT_MISMATCH,
    CXPR_BULK_SNAPSHOT_STATE_STRIDE_TOO_SMALL,
    CXPR_BULK_SNAPSHOT_CHECKSUM_MISMATCH,
    CXPR_BULK_SNAPSHOT_ALLOCATION_FAILED
} cxpr_bulk_snapshot_status;

const char* cxpr_bulk_snapshot_status_message(cxpr_bulk_snapshot_status status);

/** Serialize every element's active state bytes in element order. */
cxpr_bulk_snapshot_status cxpr_bulk_snapshot_create(
    const cxpr_generated_model_descriptor* descriptor,
    uint64_t state_layout_id,
    const cxpr_bulk_view* view,
    cxpr_bulk_snapshot* out_snapshot);

/**
 * Restore all element states, rejecting incompatible model layouts.
 *
 * `state_layout_id` is a stable, non-zero artifact identifier owned by the
 * generator or host. It must change whenever the generated state layout
 * changes, including changes that preserve `state_size()`.
 */
cxpr_bulk_snapshot_status cxpr_bulk_snapshot_restore(
    const cxpr_generated_model_descriptor* descriptor,
    uint64_t state_layout_id,
    const cxpr_bulk_snapshot* snapshot,
    const cxpr_bulk_view* view);

void cxpr_bulk_snapshot_free(cxpr_bulk_snapshot* snapshot);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_BULK_SNAPSHOT_H */
