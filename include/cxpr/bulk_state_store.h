/**
 * @file bulk_state_store.h
 * @brief Deterministic host-neutral storage for dynamic bulk populations.
 */

#ifndef CXPR_BULK_STATE_STORE_H
#define CXPR_BULK_STATE_STORE_H

#include <cxpr/bulk.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cxpr_bulk_state_store cxpr_bulk_state_store;

typedef enum cxpr_bulk_state_store_status {
    CXPR_BULK_STATE_STORE_OK = 0,
    CXPR_BULK_STATE_STORE_INVALID_ARGUMENT,
    CXPR_BULK_STATE_STORE_INVALID_DESCRIPTOR,
    CXPR_BULK_STATE_STORE_DUPLICATE_ID,
    CXPR_BULK_STATE_STORE_ID_NOT_FOUND,
    CXPR_BULK_STATE_STORE_ALLOCATION_FAILED
} cxpr_bulk_state_store_status;

const char* cxpr_bulk_state_store_status_message(
    cxpr_bulk_state_store_status status);

/** Create an empty store. Iteration order is always ascending agent ID. */
cxpr_bulk_state_store* cxpr_bulk_state_store_new(
    const cxpr_generated_model_descriptor* descriptor,
    cxpr_bulk_state_store_status* out_status);

void cxpr_bulk_state_store_free(cxpr_bulk_state_store* store);

size_t cxpr_bulk_state_store_count(const cxpr_bulk_state_store* store);
size_t cxpr_bulk_state_store_state_stride(const cxpr_bulk_state_store* store);

/** Borrowed sorted ID array, invalidated by add/remove. */
const uint64_t* cxpr_bulk_state_store_ids(const cxpr_bulk_state_store* store);

/** Add and reset a state block. Existing agents retain their exact state. */
cxpr_bulk_state_store_status cxpr_bulk_state_store_add(
    cxpr_bulk_state_store* store,
    uint64_t agent_id);

cxpr_bulk_state_store_status cxpr_bulk_state_store_remove(
    cxpr_bulk_state_store* store,
    uint64_t agent_id);

/** Borrowed state block, invalidated by add/remove. */
void* cxpr_bulk_state_store_find(
    cxpr_bulk_state_store* store,
    uint64_t agent_id);
const void* cxpr_bulk_state_store_find_const(
    const cxpr_bulk_state_store* store,
    uint64_t agent_id);

/** Attach the store's ordered state blocks and element count to a bulk view. */
cxpr_bulk_state_store_status cxpr_bulk_state_store_bind_view(
    cxpr_bulk_state_store* store,
    cxpr_bulk_view* view);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_BULK_STATE_STORE_H */
