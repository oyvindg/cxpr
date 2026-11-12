#include <cxpr/view_lease.h>

#include <stddef.h>

void cxpr_view_execution_status_reset(
    cxpr_view_execution_status* status) {
    if (!status) return;
    status->code = CXPR_VIEW_OK;
    status->view_slot = SIZE_MAX;
    status->location = SIZE_MAX;
}

cxpr_view_status cxpr_view_lease_acquire(
    const cxpr_view_table* table,
    uint64_t expected_generation,
    cxpr_view_execution_status* status,
    cxpr_view_lease* out_lease) {
    if (!table || !out_lease || (table->count > 0u && !table->views))
        return CXPR_VIEW_NULL_ARGUMENT;
    if (table->generation != expected_generation)
        return CXPR_VIEW_STALE_BINDING;
    out_lease->table = table;
    out_lease->generation = expected_generation;
    out_lease->status = status;
    return CXPR_VIEW_OK;
}

static void cxpr_view_record_first(
    cxpr_view_execution_status* sink,
    cxpr_view_status code,
    size_t slot,
    size_t location) {
    if (!sink || sink->code != CXPR_VIEW_OK) return;
    sink->code = code;
    sink->view_slot = slot;
    sink->location = location;
}

static cxpr_value cxpr_view_zero(cxpr_value_type type) {
    switch (type) {
    case CXPR_VALUE_BOOL: return cxpr_bool(false);
    case CXPR_VALUE_INT64: return cxpr_int64(0);
    case CXPR_VALUE_NUMBER:
    default: return cxpr_num(0.0);
    }
}

cxpr_view_status cxpr_view_lease_load_contained(
    const cxpr_view_lease* lease,
    size_t view_slot,
    const double* indices,
    size_t index_count,
    size_t location,
    cxpr_value* out_value) {
    cxpr_view_status result;
    cxpr_value_type type = CXPR_VALUE_NUMBER;
    if (!lease || !lease->table || !out_value)
        return CXPR_VIEW_NULL_ARGUMENT;
    if (lease->generation != lease->table->generation) {
        result = CXPR_VIEW_STALE_BINDING;
    } else {
        if (view_slot < lease->table->count)
            type = lease->table->views[view_slot].element_type;
        result = cxpr_view_load_checked(
            lease->table, view_slot, indices, index_count, out_value);
    }
    if (result == CXPR_VIEW_OK) return result;
    *out_value = cxpr_view_zero(type);
    cxpr_view_record_first(lease->status, result, view_slot, location);
    return result;
}
