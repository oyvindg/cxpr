#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/generated.h>

static void tick(void* state,
                 const cxpr_value* inputs,
                 const cxpr_value* params,
                 cxpr_value* outputs) {
    (void)state;
    (void)params;
    outputs[0] = inputs[0];
}

static size_t state_size(void) {
    return 0u;
}

static void view_tick(void* state,
                               const cxpr_value* inputs,
                               const cxpr_value* params,
                               cxpr_value* outputs,
                               const cxpr_view_table* views) {
    (void)views;
    tick(state, inputs, params, outputs);
}

static cxpr_generated_model_descriptor valid_descriptor(void) {
    cxpr_generated_model_descriptor descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.name = "identity";
    descriptor.tick = tick;
    descriptor.state_size = state_size;
    descriptor.input_names[0] = "value";
    descriptor.input_types[0] = CXPR_GENERATED_VALUE_NUMBER;
    descriptor.input_count = 1u;
    descriptor.output_names[0] = "value";
    descriptor.output_types[0] = CXPR_GENERATED_VALUE_NUMBER;
    descriptor.output_count = 1u;
    descriptor.abi_version = CXPR_GENERATED_MODEL_ABI_VERSION;
    return descriptor;
}

int main(void) {
    cxpr_generated_model_descriptor descriptor = valid_descriptor();

    assert(cxpr_generated_model_descriptor_abi_valid(&descriptor));
    descriptor.abi_version++;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.input_count = CXPR_GENERATED_MODEL_MAX_INPUTS + 1u;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.output_count = CXPR_GENERATED_MODEL_MAX_OUTPUTS + 1u;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.param_count = CXPR_GENERATED_MODEL_MAX_PARAMS + 1u;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.tick = NULL;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.output_types[0] = CXPR_GENERATED_VALUE_UNKNOWN;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.input_names[0] = NULL;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));

    descriptor = valid_descriptor();
    descriptor.view_count = 1u;
    descriptor.view_abi_version = CXPR_VIEW_ABI_VERSION;
    descriptor.view_tick = view_tick;
    descriptor.views[0].name = "samples";
    descriptor.views[0].element_type = CXPR_VALUE_NUMBER;
    descriptor.views[0].rank = 1u;
    descriptor.views[0].max_shape[0] = 1024u;
    descriptor.views[0].min_alignment = _Alignof(double);
    descriptor.views[0].access = CXPR_VIEW_ACCESS_READ_ONLY;
    assert(cxpr_generated_model_descriptor_abi_valid(&descriptor));
    descriptor.view_tick = NULL;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));
    descriptor.view_tick = view_tick;
    descriptor.views[0].max_shape[0] = 0u;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));
    assert(!cxpr_generated_model_descriptor_abi_valid(NULL));

    puts("generated descriptor ABI tests passed");
    return 0;
}
