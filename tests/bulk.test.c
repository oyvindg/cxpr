#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>

typedef struct cell_state { double previous; } cell_state;

static size_t cell_state_size(void) { return sizeof(cell_state); }

static void cell_tick(void* opaque, const double* in, const double* params,
                      double* out) {
    cell_state* state = (cell_state*)opaque;
    const double laplacian = (in[1] - 2.0 * in[0] + in[2]) / (params[0] * params[0]);
    out[0] = in[0] + params[1] * laplacian;
    out[1] = state->previous;
    state->previous = out[0];
}

static const cxpr_generated_model_descriptor descriptor = {
    .name = "bulk_cell",
    .tick = cell_tick,
    .state_size = cell_state_size,
    .input_names = {"center", "left", "right"},
    .input_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_NUMBER,
                    CXPR_GENERATED_VALUE_NUMBER},
    .input_count = 3u,
    .output_names = {"next", "previous"},
    .output_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_NUMBER},
    .output_count = 2u,
    .param_names = {"dx", "dt"},
    .param_types = {CXPR_GENERATED_VALUE_NUMBER, CXPR_GENERATED_VALUE_NUMBER},
    .param_count = 2u,
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
};

int main(void) {
    const double center[] = {1.0, 2.0, 4.0};
    const double left[] = {0.0, 1.0, 2.0};
    const double right[] = {2.0, 4.0, 8.0};
    const double params[] = {1.0, 0.25};
    double next[3] = {0};
    double previous[3] = {0};
    cell_state states[3] = {{10.0}, {20.0}, {30.0}};
    const cxpr_bulk_const_column inputs[] = {
        {center, 1u}, {left, 1u}, {right, 1u}
    };
    cxpr_bulk_column outputs[] = {{next, 1u}, {previous, 1u}};
    const cxpr_bulk_view view = {
        inputs, 3u, params, 2u, outputs, 2u,
        states, sizeof(states[0]), 3u
    };

    assert(cxpr_bulk_validate(&descriptor, &view) == CXPR_BULK_OK);
    assert(cxpr_bulk_run_range(&descriptor, &view, 1u, 2u) == CXPR_BULK_OK);
    assert(next[0] == 0.0 && next[1] == 2.25 && next[2] == 4.5);
    assert(previous[0] == 0.0 && previous[1] == 20.0 && previous[2] == 30.0);
    assert(states[0].previous == 10.0);
    assert(states[1].previous == 2.25 && states[2].previous == 4.5);
    assert(cxpr_bulk_run_range(&descriptor, &view, 3u, 1u) ==
           CXPR_BULK_RANGE_OUT_OF_BOUNDS);
    puts("bulk execution tests passed");
    return 0;
}
