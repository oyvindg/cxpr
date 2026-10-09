#include <cxpr/generated.h>

#include <assert.h>
#include <stdlib.h>

static size_t example_state_size(void) { return sizeof(double); }

static void example_tick(void* state, const cxpr_value* inputs,
                         const cxpr_value* params, cxpr_value* outputs) {
    double* total = state;
    assert(inputs[0].type == CXPR_VALUE_NUMBER);
    assert(inputs[1].type == CXPR_VALUE_BOOL);
    assert(params[0].type == CXPR_VALUE_NUMBER);
    *total += inputs[0].d * params[0].d;
    outputs[0] = cxpr_num(*total);
    outputs[1] = cxpr_bool(inputs[1].b);
}

static const cxpr_generated_model_descriptor example_descriptor = {
    .name = "generated_descriptor_host_example",
    .tick = example_tick,
    .state_size = example_state_size,
    .param_count = 1u,
    .input_names = {"value", "enabled"},
    .input_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_BOOL},
    .input_count = 2u,
    .output_names = {"total", "enabled"},
    .output_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_BOOL},
    .output_count = 2u,
    .param_names = {"scale"},
    .param_defaults = {2.0},
    .param_types = {CXPR_GENERATED_VALUE_NUMBER},
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
};

int main(void) {
    const cxpr_generated_model_descriptor* model = &example_descriptor;
    void* state = calloc(1u, model->state_size());
    cxpr_value inputs[] = {cxpr_num(2.5), cxpr_bool(true)};
    cxpr_value params[] = {cxpr_num(1.0)};
    cxpr_value outputs[2] = {cxpr_num(0.0), cxpr_num(0.0)};

    assert(state);
    assert(cxpr_generated_model_descriptor_abi_valid(model));
    model->tick(state, inputs, params, outputs);
    assert(outputs[0].type == CXPR_VALUE_NUMBER && outputs[0].d == 2.5);
    assert(outputs[1].type == CXPR_VALUE_BOOL && outputs[1].b);
    free(state);
    return 0;
}
