#include <cxpr/bulk.h>

#include <stdint.h>
#include <stdlib.h>

const char* cxpr_bulk_status_message(cxpr_bulk_status status) {
    switch (status) {
    case CXPR_BULK_OK: return "bulk view is valid";
    case CXPR_BULK_INVALID_ARGUMENT: return "invalid bulk argument";
    case CXPR_BULK_INVALID_DESCRIPTOR: return "invalid generated model descriptor";
    case CXPR_BULK_SCHEMA_MISMATCH: return "bulk view does not match model schema";
    case CXPR_BULK_MISSING_BUFFER: return "bulk view requires a data buffer";
    case CXPR_BULK_STATE_STRIDE_TOO_SMALL: return "bulk state stride is smaller than model state";
    case CXPR_BULK_RANGE_OUT_OF_BOUNDS: return "bulk range is out of bounds";
    default: return "unknown bulk execution error";
    }
}

cxpr_bulk_status cxpr_bulk_validate(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view) {
    size_t i;
    size_t state_size;
    if (!view) return CXPR_BULK_INVALID_ARGUMENT;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor))
        return CXPR_BULK_INVALID_DESCRIPTOR;
    if (view->input_count != descriptor->input_count ||
        view->output_count != descriptor->output_count ||
        view->param_count != descriptor->param_count)
        return CXPR_BULK_SCHEMA_MISMATCH;
    if ((view->input_count && !view->inputs) ||
        (view->output_count && !view->outputs) ||
        (view->param_count && !view->params))
        return CXPR_BULK_MISSING_BUFFER;
    for (i = 0u; i < view->input_count; ++i)
        if (!view->inputs[i].values) return CXPR_BULK_MISSING_BUFFER;
    for (i = 0u; i < view->output_count; ++i)
        if (!view->outputs[i].values ||
            (view->element_count > 1u && view->outputs[i].stride == 0u))
            return CXPR_BULK_MISSING_BUFFER;
    state_size = descriptor->state_size();
    if (state_size && !view->states) return CXPR_BULK_MISSING_BUFFER;
    if (state_size && view->state_stride < state_size)
        return CXPR_BULK_STATE_STRIDE_TOO_SMALL;
    if (view->element_count > 0u && view->state_stride > 0u &&
        view->element_count - 1u > SIZE_MAX / view->state_stride)
        return CXPR_BULK_INVALID_ARGUMENT;
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_run_range(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view,
    size_t begin,
    size_t count) {
    cxpr_bulk_status status = cxpr_bulk_validate(descriptor, view);
    double* inputs;
    double* outputs;
    size_t end;
    size_t i;
    size_t j;
    if (status != CXPR_BULK_OK) return status;
    if (begin > view->element_count || count > view->element_count - begin)
        return CXPR_BULK_RANGE_OUT_OF_BOUNDS;
    if (count == 0u) return CXPR_BULK_OK;
    end = begin + count;
    inputs = descriptor->input_count
        ? (double*)malloc(descriptor->input_count * sizeof(*inputs)) : NULL;
    outputs = descriptor->output_count
        ? (double*)malloc(descriptor->output_count * sizeof(*outputs)) : NULL;
    if ((descriptor->input_count && !inputs) ||
        (descriptor->output_count && !outputs)) {
        free(inputs);
        free(outputs);
        return CXPR_BULK_MISSING_BUFFER;
    }
    for (i = begin; i < end; ++i) {
        void* state = view->states
            ? (void*)((unsigned char*)view->states + i * view->state_stride)
            : NULL;
        for (j = 0u; j < descriptor->input_count; ++j) {
            const size_t index = view->inputs[j].stride ? i * view->inputs[j].stride : 0u;
            inputs[j] = view->inputs[j].values[index];
        }
        descriptor->tick(state, inputs, view->params, outputs);
        for (j = 0u; j < descriptor->output_count; ++j)
            view->outputs[j].values[i * view->outputs[j].stride] = outputs[j];
    }
    free(inputs);
    free(outputs);
    return CXPR_BULK_OK;
}

cxpr_bulk_status cxpr_bulk_run(
    const cxpr_generated_model_descriptor* descriptor,
    const cxpr_bulk_view* view) {
    return cxpr_bulk_run_range(
        descriptor, view, 0u, view ? view->element_count : 0u);
}
