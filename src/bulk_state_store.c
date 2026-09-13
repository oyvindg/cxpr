#include <cxpr/bulk_state_store.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cxpr_bulk_state_store {
    const cxpr_generated_model_descriptor* descriptor;
    uint64_t* ids;
    unsigned char* states;
    size_t state_size;
    size_t state_stride;
    size_t count;
    size_t capacity;
};

static size_t aligned_stride(size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    if (size == 0u) return 0u;
    if (size > SIZE_MAX - (alignment - 1u)) return 0u;
    return (size + alignment - 1u) / alignment * alignment;
}

static size_t lower_bound(const cxpr_bulk_state_store* store,
                          uint64_t id,
                          int* found) {
    size_t first = 0u;
    size_t count = store->count;
    while (count > 0u) {
        const size_t step = count / 2u;
        const size_t index = first + step;
        if (store->ids[index] < id) {
            first = index + 1u;
            count -= step + 1u;
        } else {
            count = step;
        }
    }
    *found = first < store->count && store->ids[first] == id;
    return first;
}

static int reserve(cxpr_bulk_state_store* store, size_t capacity) {
    uint64_t* ids;
    unsigned char* states = NULL;
    if (capacity <= store->capacity) return 1;
    if (capacity > SIZE_MAX / sizeof(*ids) ||
        (store->state_stride && capacity > SIZE_MAX / store->state_stride))
        return 0;
    ids = (uint64_t*)malloc(capacity * sizeof(*ids));
    if (!ids) return 0;
    if (store->state_stride) {
        states = (unsigned char*)malloc(capacity * store->state_stride);
        if (!states) {
            free(ids);
            return 0;
        }
    }
    if (store->count) memcpy(ids, store->ids, store->count * sizeof(*ids));
    if (store->count && store->state_stride)
        memcpy(states, store->states, store->count * store->state_stride);
    free(store->ids);
    free(store->states);
    store->ids = ids;
    store->states = states;
    store->capacity = capacity;
    return 1;
}

const char* cxpr_bulk_state_store_status_message(
    cxpr_bulk_state_store_status status) {
    switch (status) {
    case CXPR_BULK_STATE_STORE_OK: return "bulk state store operation succeeded";
    case CXPR_BULK_STATE_STORE_INVALID_ARGUMENT: return "invalid bulk state store argument";
    case CXPR_BULK_STATE_STORE_INVALID_DESCRIPTOR: return "invalid generated model descriptor";
    case CXPR_BULK_STATE_STORE_DUPLICATE_ID: return "bulk state store agent ID already exists";
    case CXPR_BULK_STATE_STORE_ID_NOT_FOUND: return "bulk state store agent ID was not found";
    case CXPR_BULK_STATE_STORE_ALLOCATION_FAILED: return "bulk state store allocation failed";
    default: return "unknown bulk state store error";
    }
}

cxpr_bulk_state_store* cxpr_bulk_state_store_new(
    const cxpr_generated_model_descriptor* descriptor,
    cxpr_bulk_state_store_status* out_status) {
    cxpr_bulk_state_store* store;
    size_t state_size;
    size_t stride;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor)) {
        if (out_status) *out_status = CXPR_BULK_STATE_STORE_INVALID_DESCRIPTOR;
        return NULL;
    }
    state_size = descriptor->state_size();
    stride = aligned_stride(state_size);
    if (state_size && !stride) {
        if (out_status) *out_status = CXPR_BULK_STATE_STORE_ALLOCATION_FAILED;
        return NULL;
    }
    store = (cxpr_bulk_state_store*)calloc(1u, sizeof(*store));
    if (!store) {
        if (out_status) *out_status = CXPR_BULK_STATE_STORE_ALLOCATION_FAILED;
        return NULL;
    }
    store->descriptor = descriptor;
    store->state_size = state_size;
    store->state_stride = stride;
    if (out_status) *out_status = CXPR_BULK_STATE_STORE_OK;
    return store;
}

void cxpr_bulk_state_store_free(cxpr_bulk_state_store* store) {
    if (!store) return;
    free(store->ids);
    free(store->states);
    free(store);
}

size_t cxpr_bulk_state_store_count(const cxpr_bulk_state_store* store) {
    return store ? store->count : 0u;
}

size_t cxpr_bulk_state_store_state_stride(const cxpr_bulk_state_store* store) {
    return store ? store->state_stride : 0u;
}

const uint64_t* cxpr_bulk_state_store_ids(const cxpr_bulk_state_store* store) {
    return store ? store->ids : NULL;
}

cxpr_bulk_state_store_status cxpr_bulk_state_store_add(
    cxpr_bulk_state_store* store,
    uint64_t agent_id) {
    int found;
    size_t index;
    size_t capacity;
    unsigned char* state;
    if (!store) return CXPR_BULK_STATE_STORE_INVALID_ARGUMENT;
    index = lower_bound(store, agent_id, &found);
    if (found) return CXPR_BULK_STATE_STORE_DUPLICATE_ID;
    if (store->count == store->capacity) {
        capacity = store->capacity ? store->capacity * 2u : 8u;
        if (capacity < store->capacity || !reserve(store, capacity))
            return CXPR_BULK_STATE_STORE_ALLOCATION_FAILED;
    }
    memmove(store->ids + index + 1u, store->ids + index,
            (store->count - index) * sizeof(*store->ids));
    store->ids[index] = agent_id;
    if (store->state_stride) {
        memmove(store->states + (index + 1u) * store->state_stride,
                store->states + index * store->state_stride,
                (store->count - index) * store->state_stride);
        state = store->states + index * store->state_stride;
        memset(state, 0, store->state_stride);
        if (store->descriptor->reset) store->descriptor->reset(state);
    }
    store->count++;
    return CXPR_BULK_STATE_STORE_OK;
}

cxpr_bulk_state_store_status cxpr_bulk_state_store_remove(
    cxpr_bulk_state_store* store,
    uint64_t agent_id) {
    int found;
    size_t index;
    if (!store) return CXPR_BULK_STATE_STORE_INVALID_ARGUMENT;
    index = lower_bound(store, agent_id, &found);
    if (!found) return CXPR_BULK_STATE_STORE_ID_NOT_FOUND;
    memmove(store->ids + index, store->ids + index + 1u,
            (store->count - index - 1u) * sizeof(*store->ids));
    if (store->state_stride)
        memmove(store->states + index * store->state_stride,
                store->states + (index + 1u) * store->state_stride,
                (store->count - index - 1u) * store->state_stride);
    store->count--;
    return CXPR_BULK_STATE_STORE_OK;
}

void* cxpr_bulk_state_store_find(cxpr_bulk_state_store* store,
                                 uint64_t agent_id) {
    int found;
    size_t index;
    if (!store) return NULL;
    index = lower_bound(store, agent_id, &found);
    if (!found || !store->state_stride) return NULL;
    return store->states + index * store->state_stride;
}

const void* cxpr_bulk_state_store_find_const(
    const cxpr_bulk_state_store* store,
    uint64_t agent_id) {
    return cxpr_bulk_state_store_find((cxpr_bulk_state_store*)store, agent_id);
}

cxpr_bulk_state_store_status cxpr_bulk_state_store_bind_view(
    cxpr_bulk_state_store* store,
    cxpr_bulk_view* view) {
    if (!store || !view) return CXPR_BULK_STATE_STORE_INVALID_ARGUMENT;
    view->states = store->states;
    view->state_stride = store->state_stride;
    view->element_count = store->count;
    return CXPR_BULK_STATE_STORE_OK;
}
