#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/generated.h>

static void tick(void* state,
                 const double* inputs,
                 const double* params,
                 double* outputs) {
    (void)state;
    (void)params;
    outputs[0] = inputs[0];
}

static size_t state_size(void) {
    return 0u;
}

static cxpr_generated_model_descriptor valid_descriptor(void) {
    cxpr_generated_model_descriptor descriptor;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.name = "identity";
    descriptor.tick = tick;
    descriptor.state_size = state_size;
    descriptor.input_names[0] = "value";
    descriptor.input_count = 1u;
    descriptor.output_names[0] = "value";
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
    descriptor.tick = NULL;
    assert(!cxpr_generated_model_descriptor_abi_valid(&descriptor));
    assert(!cxpr_generated_model_descriptor_abi_valid(NULL));

    puts("generated descriptor ABI tests passed");
    return 0;
}
