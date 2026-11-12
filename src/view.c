#include <cxpr/view.h>

#include <stdbool.h>
#include <math.h>
#include <stdint.h>

static size_t cxpr_view_element_size(cxpr_value_type type) {
    switch (type) {
    case CXPR_VALUE_NUMBER: return sizeof(double);
    case CXPR_VALUE_INT64: return sizeof(int64_t);
    case CXPR_VALUE_BOOL: return sizeof(bool);
    default: return 0u;
    }
}

const char* cxpr_view_status_message(cxpr_view_status status) {
    switch (status) {
    case CXPR_VIEW_OK: return "external view binding is valid";
    case CXPR_VIEW_NULL_ARGUMENT: return "external view argument is NULL";
    case CXPR_VIEW_INVALID_REQUIREMENT: return "external view requirement is invalid";
    case CXPR_VIEW_MISSING_BINDING: return "external view storage is missing";
    case CXPR_VIEW_TYPE_MISMATCH: return "external view element type does not match";
    case CXPR_VIEW_RANK_MISMATCH: return "external view rank does not match";
    case CXPR_VIEW_SHAPE_EXCEEDED: return "external view active shape exceeds its declared maximum";
    case CXPR_VIEW_INVALID_FLAGS: return "external view must be read-only and contiguous";
    case CXPR_VIEW_NON_CONTIGUOUS: return "external view byte strides are not contiguous row-major";
    case CXPR_VIEW_CAPACITY_EXCEEDED: return "external view capacity is too small";
    case CXPR_VIEW_MISALIGNED: return "external view storage does not meet alignment requirement";
    case CXPR_VIEW_OVERFLOW: return "external view shape overflows addressable storage";
    case CXPR_VIEW_STALE_BINDING: return "external view binding is missing or stale";
    case CXPR_VIEW_INVALID_INDEX: return "external view index is not a finite non-negative integer";
    case CXPR_VIEW_INDEX_OUT_OF_RANGE: return "external view index is outside its active shape";
    default: return "unknown external view error";
    }
}

static cxpr_view_status cxpr_view_at(
    const cxpr_view_table* table,
    size_t slot,
    const cxpr_view_resolved** out) {
    if (!table || !out) return CXPR_VIEW_NULL_ARGUMENT;
    if (!table->views || slot >= table->count)
        return CXPR_VIEW_STALE_BINDING;
    *out = &table->views[slot];
    if ((*out)->rank == 0u || (*out)->rank > CXPR_VIEW_MAX_RANK ||
        cxpr_view_element_size((*out)->element_type) == 0u) {
        return CXPR_VIEW_STALE_BINDING;
    }
    if (!(*out)->data) {
        bool empty = false;
        for (size_t i = 0u; i < (*out)->rank; ++i)
            empty = empty || (*out)->shape[i] == 0u;
        if (!empty) return CXPR_VIEW_STALE_BINDING;
    }
    return CXPR_VIEW_OK;
}

cxpr_view_status cxpr_view_length(
    const cxpr_view_table* table,
    size_t slot,
    size_t dimension,
    size_t* out_length) {
    const cxpr_view_resolved* view;
    cxpr_view_status status;
    if (!out_length) return CXPR_VIEW_NULL_ARGUMENT;
    status = cxpr_view_at(table, slot, &view);
    if (status != CXPR_VIEW_OK) return status;
    if (dimension >= view->rank) return CXPR_VIEW_RANK_MISMATCH;
    *out_length = view->shape[dimension];
    return CXPR_VIEW_OK;
}

static cxpr_view_status cxpr_view_offset(
    const cxpr_view_resolved* view,
    const double* indices,
    size_t index_count,
    bool clamp,
    size_t* out_offset) {
    size_t offset = 0u;
    if ((!indices && index_count > 0u) || !out_offset)
        return CXPR_VIEW_NULL_ARGUMENT;
    if (index_count != view->rank) return CXPR_VIEW_RANK_MISMATCH;
    for (size_t i = 0u; i < index_count; ++i) {
        double raw = indices[i];
        size_t index;
        if (!isfinite(raw) || (!clamp && raw < 0.0) || raw > (double)SIZE_MAX ||
            floor(raw) != raw) {
            return CXPR_VIEW_INVALID_INDEX;
        }
        index = raw < 0.0 ? 0u : (size_t)raw;
        if (index >= view->shape[i]) {
            if (!clamp || view->shape[i] == 0u)
                return CXPR_VIEW_INDEX_OUT_OF_RANGE;
            index = view->shape[i] - 1u;
        }
        if (index != 0u && view->element_strides[i] > SIZE_MAX / index)
            return CXPR_VIEW_OVERFLOW;
        const size_t term = index * view->element_strides[i];
        if (offset > SIZE_MAX - term) return CXPR_VIEW_OVERFLOW;
        offset += term;
    }
    *out_offset = offset;
    return CXPR_VIEW_OK;
}

static cxpr_view_status cxpr_view_read(
    const cxpr_view_resolved* view,
    size_t offset,
    cxpr_value* out_value) {
    if (!out_value) return CXPR_VIEW_NULL_ARGUMENT;
    switch (view->element_type) {
    case CXPR_VALUE_NUMBER:
        *out_value = cxpr_num(((const double*)view->data)[offset]);
        return CXPR_VIEW_OK;
    case CXPR_VALUE_INT64:
        *out_value = cxpr_int64(((const int64_t*)view->data)[offset]);
        return CXPR_VIEW_OK;
    case CXPR_VALUE_BOOL:
        *out_value = cxpr_bool(((const bool*)view->data)[offset]);
        return CXPR_VIEW_OK;
    default: return CXPR_VIEW_STALE_BINDING;
    }
}

static cxpr_view_status cxpr_view_load(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    bool clamp,
    cxpr_value* out_value) {
    const cxpr_view_resolved* view;
    cxpr_view_status status = cxpr_view_at(table, slot, &view);
    size_t offset;
    if (status != CXPR_VIEW_OK) return status;
    status = cxpr_view_offset(view, indices, index_count, clamp, &offset);
    if (status != CXPR_VIEW_OK) return status;
    return cxpr_view_read(view, offset, out_value);
}

cxpr_view_status cxpr_view_load_checked(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value* out_value) {
    return cxpr_view_load(
        table, slot, indices, index_count, false, out_value);
}

cxpr_view_status cxpr_view_load_or(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value fallback,
    cxpr_value* out_value) {
    const cxpr_view_resolved* view;
    cxpr_view_status status;
    if (!out_value) return CXPR_VIEW_NULL_ARGUMENT;
    status = cxpr_view_at(table, slot, &view);
    if (status != CXPR_VIEW_OK) return status;
    if (fallback.type != view->element_type)
        return CXPR_VIEW_TYPE_MISMATCH;
    status = cxpr_view_load(
        table, slot, indices, index_count, false, out_value);
    if (status == CXPR_VIEW_INVALID_INDEX ||
        status == CXPR_VIEW_INDEX_OUT_OF_RANGE) {
        *out_value = fallback;
    }
    return status;
}

cxpr_view_status cxpr_view_load_clamped(
    const cxpr_view_table* table,
    size_t slot,
    const double* indices,
    size_t index_count,
    cxpr_value* out_value) {
    return cxpr_view_load(
        table, slot, indices, index_count, true, out_value);
}

static cxpr_view_status cxpr_view_resolve_one(
    const cxpr_view_requirement* requirement,
    const cxpr_view* binding,
    cxpr_view_resolved* resolved) {
    size_t element_size;
    size_t elements = 1u;
    size_t expected_stride;

    if (!requirement->name || requirement->rank == 0u ||
        requirement->rank > CXPR_VIEW_MAX_RANK ||
        requirement->slot >= CXPR_VIEW_MAX_COUNT ||
        requirement->access != CXPR_VIEW_ACCESS_READ_ONLY ||
        requirement->min_alignment == 0u ||
        (requirement->min_alignment & (requirement->min_alignment - 1u)) != 0u) {
        return CXPR_VIEW_INVALID_REQUIREMENT;
    }
    element_size = cxpr_view_element_size(requirement->element_type);
    if (element_size == 0u) return CXPR_VIEW_INVALID_REQUIREMENT;
    if (binding->element_type != requirement->element_type)
        return CXPR_VIEW_TYPE_MISMATCH;
    if (binding->rank != requirement->rank)
        return CXPR_VIEW_RANK_MISMATCH;
    if ((binding->flags & (CXPR_VIEW_ACCESS_READ_ONLY | CXPR_VIEW_CONTIGUOUS)) !=
            (CXPR_VIEW_ACCESS_READ_ONLY | CXPR_VIEW_CONTIGUOUS) ||
        (binding->flags & CXPR_VIEW_WRITABLE) != 0u) {
        return CXPR_VIEW_INVALID_FLAGS;
    }

    expected_stride = element_size;
    for (size_t reverse = requirement->rank; reverse > 0u; --reverse) {
        const size_t dimension = reverse - 1u;
        const size_t active = binding->shape[dimension];
        if (requirement->max_shape[dimension] == 0u)
            return CXPR_VIEW_INVALID_REQUIREMENT;
        if (active > requirement->max_shape[dimension])
            return CXPR_VIEW_SHAPE_EXCEEDED;
        if (binding->strides[dimension] < 0 ||
            (size_t)binding->strides[dimension] != expected_stride)
            return CXPR_VIEW_NON_CONTIGUOUS;
        resolved->shape[dimension] = active;
        resolved->element_strides[dimension] = expected_stride / element_size;
        if (active != 0u && elements > SIZE_MAX / active)
            return CXPR_VIEW_OVERFLOW;
        elements *= active;
        if (active != 0u && expected_stride > SIZE_MAX / active)
            return CXPR_VIEW_OVERFLOW;
        expected_stride *= active;
    }
    if (elements != 0u && !binding->data)
        return CXPR_VIEW_MISSING_BINDING;
    if (elements > SIZE_MAX / element_size)
        return CXPR_VIEW_OVERFLOW;
    if (binding->capacity_bytes < elements * element_size)
        return CXPR_VIEW_CAPACITY_EXCEEDED;
    if (binding->data &&
        ((uintptr_t)binding->data & (requirement->min_alignment - 1u)) != 0u)
        return CXPR_VIEW_MISALIGNED;

    resolved->data = binding->data;
    resolved->element_type = binding->element_type;
    resolved->rank = binding->rank;
    return CXPR_VIEW_OK;
}

cxpr_view_status cxpr_views_resolve(
    const cxpr_view_requirement* requirements,
    const cxpr_view* bindings,
    size_t count,
    cxpr_view_resolved* resolved,
    size_t* failed_slot) {
    if (failed_slot) *failed_slot = SIZE_MAX;
    if (count > CXPR_VIEW_MAX_COUNT)
        return CXPR_VIEW_INVALID_REQUIREMENT;
    if (count > 0u && (!requirements || !bindings || !resolved))
        return CXPR_VIEW_NULL_ARGUMENT;

    for (size_t i = 0u; i < count; ++i) {
        cxpr_view_status status;
        if (requirements[i].slot != i) {
            if (failed_slot) *failed_slot = requirements[i].slot;
            return CXPR_VIEW_INVALID_REQUIREMENT;
        }
        status = cxpr_view_resolve_one(
            &requirements[i], &bindings[i], &resolved[i]);
        if (status != CXPR_VIEW_OK) {
            if (failed_slot) *failed_slot = requirements[i].slot;
            return status;
        }
    }
    return CXPR_VIEW_OK;
}
