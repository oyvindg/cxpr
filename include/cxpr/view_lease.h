/**
 * @file view_lease.h
 * @brief Generation leases and bounded status for external-view execution.
 */

#ifndef CXPR_VIEW_LEASE_H
#define CXPR_VIEW_LEASE_H

#include <cxpr/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Preallocated first-error status writable from an allocation-free hot path. */
typedef struct cxpr_view_execution_status {
    cxpr_view_status code;
    size_t view_slot;
    size_t location;
} cxpr_view_execution_status;

/** Borrowed immutable table pinned to one host-controlled generation. */
typedef struct cxpr_view_lease {
    const cxpr_view_table* table;
    uint64_t generation;
    cxpr_view_execution_status* status;
} cxpr_view_lease;

/** Reset a reusable bounded execution status before a block or launch. */
void cxpr_view_execution_status_reset(
    cxpr_view_execution_status* status);

/**
 * Acquire one immutable execution lease.
 * @param table Validated table published by the host.
 * @param expected_generation Generation selected at the execution boundary.
 * @param status Optional caller-owned first-error sink.
 * @param out_lease Receives a borrowed lease on success.
 * @returns `CXPR_VIEW_STALE_BINDING` when publication changed.
 */
cxpr_view_status cxpr_view_lease_acquire(
    const cxpr_view_table* table,
    uint64_t expected_generation,
    cxpr_view_execution_status* status,
    cxpr_view_lease* out_lease);

/**
 * Checked load with real-time zero containment and first-error reporting.
 *
 * Invalid access remains observable through the returned status and sink, but
 * the output receives the view element type's zero value so a host callback
 * can finish without allocation or unwinding.
 */
cxpr_view_status cxpr_view_lease_load_contained(
    const cxpr_view_lease* lease,
    size_t view_slot,
    const double* indices,
    size_t index_count,
    size_t location,
    cxpr_value* out_value);

#ifdef __cplusplus
}
#endif

#endif
