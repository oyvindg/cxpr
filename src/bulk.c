#include <cxpr/bulk.h>

#include <stdint.h>
#include <string.h>

const char* cxpr_bulk_status_message(cxpr_bulk_status status) {
    switch (status) {
    case CXPR_BULK_OK: return "bulk view is valid";
    case CXPR_BULK_INVALID_ARGUMENT: return "invalid bulk argument";
    case CXPR_BULK_INVALID_DESCRIPTOR: return "invalid generated model descriptor";
    case CXPR_BULK_SCHEMA_MISMATCH: return "bulk view does not match model schema";
    case CXPR_BULK_MISSING_BUFFER: return "bulk view requires a data buffer";
    case CXPR_BULK_STATE_STRIDE_TOO_SMALL: return "bulk state stride is smaller than model state";
    case CXPR_BULK_RANGE_OUT_OF_BOUNDS: return "bulk range is out of bounds";
    case CXPR_BULK_VIEWS_REQUIRED:
        return "generated model requires explicit shared external views";
    case CXPR_BULK_INVALID_VIEWS:
        return "shared external views do not match the generated model";
    default: return "unknown bulk execution error";
    }
}

static cxpr_bulk_status cxpr_bulk_validate_columns(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    int allow_views) {
    size_t i;
    size_t state_size;
    if (!view) return CXPR_BULK_INVALID_ARGUMENT;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor))
        return CXPR_BULK_INVALID_DESCRIPTOR;
    if (!allow_views && descriptor->view_count > 0u)
        return CXPR_BULK_VIEWS_REQUIRED;
    if (view->input_count != descriptor->input_count ||
        view->output_count != descriptor->output_count ||
        view->param_count != descriptor->param_count)
        return CXPR_BULK_SCHEMA_MISMATCH;
    if ((view->input_count && !view->inputs) ||
        (view->output_count && !view->outputs) ||
        (view->param_count && !view->params))
        return CXPR_BULK_MISSING_BUFFER;
    if (view->param_stride && view->param_stride < view->param_count)
        return CXPR_BULK_SCHEMA_MISMATCH;
    if (view->element_count > 0u && view->param_stride > 0u &&
        view->element_count - 1u > SIZE_MAX / view->param_stride)
        return CXPR_BULK_INVALID_ARGUMENT;
    for (i = 0u; i < view->input_count; ++i)
        if (!view->inputs[i].values) return CXPR_BULK_MISSING_BUFFER;
        else if (view->element_count > 0u && view->inputs[i].stride > 0u &&
                 view->element_count - 1u > SIZE_MAX / view->inputs[i].stride)
            return CXPR_BULK_INVALID_ARGUMENT;
    for (i = 0u; i < view->output_count; ++i)
        if (!view->outputs[i].values ||
            (view->element_count > 1u && view->outputs[i].stride == 0u))
            return CXPR_BULK_MISSING_BUFFER;
        else if (view->element_count > 0u && view->outputs[i].stride > 0u &&
                 view->element_count - 1u > SIZE_MAX / view->outputs[i].stride)
            return CXPR_BULK_INVALID_ARGUMENT;
    state_size = descriptor->state_size();
    if (state_size && !view->states) return CXPR_BULK_MISSING_BUFFER;
    if (state_size && view->state_stride < state_size)
        return CXPR_BULK_STATE_STRIDE_TOO_SMALL;
    if (state_size && view->element_count > 0u && view->state_stride > 0u &&
        view->element_count - 1u > SIZE_MAX / view->state_stride)
        return CXPR_BULK_INVALID_ARGUMENT;
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_validate(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view) {
    return cxpr_bulk_validate_columns(descriptor, view, 0);
}

static int cxpr_bulk_views_match(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_view_table* table) {
    if (!table || table->count != descriptor->view_count ||
        (table->count > 0u && !table->views)) return 0;
    for (size_t i = 0u; i < table->count; ++i) {
        const cxpr_view_requirement* requirement =
            &descriptor->views[i];
        const cxpr_view_resolved* view = &table->views[i];
        int has_elements = 1;
        if (view->element_type != requirement->element_type ||
            view->rank != requirement->rank) return 0;
        for (size_t dimension = 0u; dimension < view->rank; ++dimension) {
            if (view->shape[dimension] > requirement->max_shape[dimension])
                return 0;
            if (view->shape[dimension] == 0u) has_elements = 0;
        }
        if (has_elements && !view->data) return 0;
    }
    return 1;
}

cxpr_bulk_status cxpr_bulk_validate_views(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    const cxpr_view_table* views) {
    cxpr_bulk_status status =
        cxpr_bulk_validate_columns(descriptor, view, 1);
    if (status != CXPR_BULK_OK) return status;
    return cxpr_bulk_views_match(descriptor, views)
        ? CXPR_BULK_OK : CXPR_BULK_INVALID_VIEWS;
}

cxpr_bulk_status cxpr_bulk_run_range(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    size_t begin,
    size_t count) {
    cxpr_bulk_status status = cxpr_bulk_validate(descriptor, view);
    cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS];
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t end;
    size_t i;
    size_t j;
    if (status != CXPR_BULK_OK) return status;
    if (begin > view->element_count || count > view->element_count - begin)
        return CXPR_BULK_RANGE_OUT_OF_BOUNDS;
    if (count == 0u) return CXPR_BULK_OK;
    end = begin + count;
    for (i = begin; i < end; ++i) {
        void* state = view->states
            ? (void*)((unsigned char*)view->states + i * view->state_stride)
            : NULL;
        for (j = 0u; j < descriptor->input_count; ++j) {
            const size_t index = view->inputs[j].stride ? i * view->inputs[j].stride : 0u;
            inputs[j] = view->inputs[j].values[index];
        }
        descriptor->tick(
            state, inputs,
            view->params
                ? view->params + (view->param_stride ? i * view->param_stride : 0u)
                : NULL,
            outputs);
        for (j = 0u; j < descriptor->output_count; ++j)
            view->outputs[j].values[i * view->outputs[j].stride] = outputs[j];
    }
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_run(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view) {
    return cxpr_bulk_run_range(
        descriptor, view, 0u, view ? view->element_count : 0u);
}

cxpr_bulk_status cxpr_bulk_run_range_views(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    const cxpr_view_table* views,
    size_t begin,
    size_t count) {
    cxpr_bulk_status status = cxpr_bulk_validate_views(
        descriptor, view, views);
    cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS];
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    if (status != CXPR_BULK_OK) return status;
    if (begin > view->element_count || count > view->element_count - begin)
        return CXPR_BULK_RANGE_OUT_OF_BOUNDS;
    for (size_t i = begin; i < begin + count; ++i) {
        void* state = view->states
            ? (void*)((unsigned char*)view->states + i * view->state_stride)
            : NULL;
        for (size_t j = 0u; j < descriptor->input_count; ++j) {
            const size_t index = view->inputs[j].stride
                ? i * view->inputs[j].stride : 0u;
            inputs[j] = view->inputs[j].values[index];
        }
        descriptor->view_tick(
            state, inputs,
            view->params
                ? view->params + (view->param_stride ? i * view->param_stride : 0u)
                : NULL,
            outputs, views);
        for (size_t j = 0u; j < descriptor->output_count; ++j)
            view->outputs[j].values[i * view->outputs[j].stride] = outputs[j];
    }
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_run_views(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    const cxpr_view_table* views) {
    return cxpr_bulk_run_range_views(
        descriptor, view, views, 0u,
        view ? view->element_count : 0u);
}

cxpr_bulk_status cxpr_bulk_reset_range(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    size_t begin,
    size_t count) {
    cxpr_bulk_status status = cxpr_bulk_validate(descriptor, view);
    size_t state_size;
    size_t i;
    if (status != CXPR_BULK_OK) return status;
    state_size = descriptor->state_size();
    if (begin > view->element_count || count > view->element_count - begin)
        return CXPR_BULK_RANGE_OUT_OF_BOUNDS;
    for (i = begin; state_size && i < begin + count; ++i) {
        void* state = (void*)((unsigned char*)view->states + i * view->state_stride);
        if (descriptor->reset) descriptor->reset(state);
        else memset(state, 0, state_size);
    }
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_reset(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view) {
    return cxpr_bulk_reset_range(
        descriptor, view, 0u, view ? view->element_count : 0u);
}
