#include <assert.h>
#include <stdlib.h>

#include <cxpr/generated.h>

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        cxpr_generated_model_v1();
    double samples[2][3][2] = {
        {{1.0, 1.25}, {2.0, 2.5}, {4.0, 5.0}},
        {{8.0, 10.0}, {16.0, 20.0}, {32.0, 40.0}}
    };
    cxpr_view binding = {0};
    cxpr_view_resolved resolved = {0};
    cxpr_view_table table = {0};
    cxpr_value inputs[] = {cxpr_int64(1), cxpr_int64(2), cxpr_int64(0)};
    cxpr_value outputs[] = {
        cxpr_num(0.0), cxpr_num(0.0), cxpr_num(0.0), cxpr_num(0.0)};
    void* state;

    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->tick == NULL);
    assert(descriptor->view_tick != NULL);
    assert(descriptor->input_count == 3u);
    assert(descriptor->view_count == 1u);
    assert(descriptor->output_count == 4u);
    assert(descriptor->views[0].rank == 3u);
    assert(descriptor->views[0].max_shape[0] == 4u);
    assert(descriptor->views[0].max_shape[1] == 8u);
    assert(descriptor->views[0].max_shape[2] == 2u);

    binding.data = samples;
    binding.element_type = CXPR_VALUE_NUMBER;
    binding.rank = 3u;
    binding.shape[0] = 2u;
    binding.shape[1] = 3u;
    binding.shape[2] = 2u;
    binding.strides[0] = 6 * (ptrdiff_t)sizeof(double);
    binding.strides[1] = 2 * (ptrdiff_t)sizeof(double);
    binding.strides[2] = (ptrdiff_t)sizeof(double);
    binding.capacity_bytes = sizeof(samples);
    binding.flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS;
    assert(cxpr_views_resolve(
        descriptor->views, &binding, 1u, &resolved, NULL) ==
        CXPR_VIEW_OK);
    table.views = &resolved;
    table.count = 1u;
    table.generation = 7u;

    state = calloc(1u, descriptor->state_size());
    assert(state != NULL);
    descriptor->view_tick(state, inputs, NULL, outputs, &table);
    assert(outputs[0].type == CXPR_VALUE_NUMBER);
    assert(outputs[0].d == 32.0);
    assert(outputs[1].type == CXPR_VALUE_NUMBER);
    assert(outputs[1].d == 2.0);
    assert(outputs[2].type == CXPR_VALUE_NUMBER && outputs[2].d == 32.0);
    assert(outputs[3].type == CXPR_VALUE_NUMBER && outputs[3].d == 32.0);

    inputs[1] = cxpr_int64(99);
    descriptor->view_tick(state, inputs, NULL, outputs, &table);
    assert(outputs[0].type == CXPR_VALUE_NUMBER);
    assert(outputs[0].d != outputs[0].d); /* Checked ordinary indexing contains as NaN. */
    assert(outputs[2].d == -7.0); /* get uses the authored typed fallback. */
    assert(outputs[3].d == 32.0); /* get_clamped selects the last active row. */
    free(state);
    return 0;
}
