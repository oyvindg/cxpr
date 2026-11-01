#include <cxpr/generated.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "codegen_regressions.gen.c"

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &codegen_regressions_tick_descriptor;
    cxpr_value inputs[] = {
        cxpr_num(-50.0),
        cxpr_num(2.0),
        cxpr_num(-100.0),
        cxpr_num(7.0),
    };
    cxpr_value output[2] = {{0}};
    void* state;
    double expected = 10.0 + 3.14159265358979323846;

    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 4u);
    assert(descriptor->output_count == 2u);
    state = calloc(1u, descriptor->state_size());
    assert(state);

    descriptor->tick(state, inputs, NULL, output);
    assert(output[0].type == CXPR_VALUE_NUMBER);
    assert(fabs(output[0].d - expected) <= 1e-12);
    assert(output[1].type == CXPR_VALUE_NUMBER);
    assert(output[1].d == 10.0);

    free(state);
    return 0;
}
