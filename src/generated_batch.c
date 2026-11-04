#include <cxpr/generated_batch.h>

#include <string.h>

const char* cxpr_generated_batch_status_message(cxpr_generated_batch_status status) {
    switch (status) {
    case CXPR_GENERATED_BATCH_OK: return "generated batch completed";
    case CXPR_GENERATED_BATCH_INVALID_ARGUMENT: return "invalid generated batch argument";
    case CXPR_GENERATED_BATCH_INVALID_DESCRIPTOR: return "invalid generated model descriptor";
    case CXPR_GENERATED_BATCH_SCHEMA_MISMATCH: return "generated batch bindings do not match the descriptor";
    case CXPR_GENERATED_BATCH_INVALID_VIEW: return "generated batch requires matching rank-one scalar views";
    default: return "unknown generated batch error";
    }
}

static cxpr_value_type value_type(cxpr_generated_value_type type) {
    switch (type) {
    case CXPR_GENERATED_VALUE_NUMBER: return CXPR_VALUE_NUMBER;
    case CXPR_GENERATED_VALUE_BOOL: return CXPR_VALUE_BOOL;
    case CXPR_GENERATED_VALUE_INT64: return CXPR_VALUE_INT64;
    default: return CXPR_VALUE_NULL;
    }
}

static int valid_view(const cxpr_view* view, cxpr_value_type type,
                      size_t rows, unsigned writable) {
    size_t element_size = type == CXPR_VALUE_NUMBER ? sizeof(double) :
        type == CXPR_VALUE_INT64 ? sizeof(int64_t) : sizeof(bool);
    if (!view || view->element_type != type || view->rank != 1u ||
        view->shape[0] != rows || view->strides[0] != (ptrdiff_t)element_size ||
        (rows && !view->data) || view->capacity_bytes < rows * element_size)
        return 0;
    return writable ? (view->flags & CXPR_VIEW_WRITABLE) != 0u
                    : (view->flags & CXPR_VIEW_READ_ONLY) != 0u;
}

static cxpr_value read_value(const cxpr_view* view, size_t row) {
    cxpr_value result = {.type = view->element_type};
    if (view->element_type == CXPR_VALUE_NUMBER) result.d = ((const double*)view->data)[row];
    else if (view->element_type == CXPR_VALUE_INT64) result.i64 = ((const int64_t*)view->data)[row];
    else result.b = ((const bool*)view->data)[row];
    return result;
}

static void write_value(cxpr_view* view, size_t row, cxpr_value value) {
    if (view->element_type == CXPR_VALUE_NUMBER) ((double*)view->data)[row] = value.d;
    else if (view->element_type == CXPR_VALUE_INT64) ((int64_t*)view->data)[row] = value.i64;
    else ((bool*)view->data)[row] = value.b;
}

cxpr_generated_batch_status cxpr_generated_tick_batch(
    const cxpr_generated_model_descriptor* descriptor, void* state,
    const cxpr_value* params, const cxpr_generated_view_binding* inputs,
    size_t input_count, cxpr_generated_view_binding* outputs,
    size_t output_count, size_t row_count) {
    cxpr_value input_values[CXPR_GENERATED_MODEL_MAX_INPUTS];
    cxpr_value output_values[CXPR_GENERATED_MODEL_MAX_OUTPUTS];
    size_t i, row;
    if (!cxpr_generated_model_descriptor_abi_valid(descriptor))
        return CXPR_GENERATED_BATCH_INVALID_DESCRIPTOR;
    if ((descriptor->state_size() && !state) ||
        (descriptor->param_count && !params) ||
        (input_count && !inputs) || (output_count && !outputs))
        return CXPR_GENERATED_BATCH_INVALID_ARGUMENT;
    if (input_count != descriptor->input_count || output_count != descriptor->output_count)
        return CXPR_GENERATED_BATCH_SCHEMA_MISMATCH;
    for (i = 0u; i < input_count; ++i)
        if (!inputs[i].name || strcmp(inputs[i].name, descriptor->input_names[i]) != 0 ||
            !valid_view(&inputs[i].view, value_type(descriptor->input_types[i]), row_count, 0u))
            return CXPR_GENERATED_BATCH_INVALID_VIEW;
    for (i = 0u; i < output_count; ++i)
        if (!outputs[i].name || strcmp(outputs[i].name, descriptor->output_names[i]) != 0 ||
            !valid_view(&outputs[i].view, value_type(descriptor->output_types[i]), row_count, 1u))
            return CXPR_GENERATED_BATCH_INVALID_VIEW;
    for (row = 0u; row < row_count; ++row) {
        for (i = 0u; i < input_count; ++i) input_values[i] = read_value(&inputs[i].view, row);
        descriptor->tick(state, input_values, params, output_values);
        for (i = 0u; i < output_count; ++i) write_value(&outputs[i].view, row, output_values[i]);
    }
    return CXPR_GENERATED_BATCH_OK;
}
