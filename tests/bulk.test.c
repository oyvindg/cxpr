#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>

typedef struct cell_state { double previous; } cell_state;

static size_t cell_state_size(void) { return sizeof(cell_state); }

static void cell_tick(void* opaque, const cxpr_value* in, const cxpr_value* params,
                      cxpr_value* out) {
    cell_state* state = (cell_state*)opaque;
    const double laplacian = (in[1].d - 2.0 * in[0].d + in[2].d) / (params[0].d * params[0].d);
    out[0] = cxpr_num(in[0].d + params[1].d * laplacian);
    out[1] = cxpr_num(state->previous);
    state->previous = out[0].d;
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

static void exact_tick(void* state, const cxpr_value* in,
                       const cxpr_value* params, cxpr_value* out) {
    (void)state;
    out[0] = cxpr_int64(in[0].i64 + params[0].i64);
}

static size_t exact_state_size(void) { return 0u; }

static const cxpr_generated_model_descriptor exact_descriptor = {
    .name = "bulk_exact_int64",
    .tick = exact_tick,
    .state_size = exact_state_size,
    .input_names = {"value"},
    .input_types = {CXPR_GENERATED_VALUE_INT64},
    .input_count = 1u,
    .output_names = {"result"},
    .output_types = {CXPR_GENERATED_VALUE_INT64},
    .output_count = 1u,
    .param_names = {"increment"},
    .param_types = {CXPR_GENERATED_VALUE_INT64},
    .param_count = 1u,
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
};

int main(void) {
    const cxpr_value center[] = {CXPR_VALUE_NUMBER_INIT(1.0), CXPR_VALUE_NUMBER_INIT(2.0), CXPR_VALUE_NUMBER_INIT(4.0)};
    const cxpr_value left[] = {CXPR_VALUE_NUMBER_INIT(0.0), CXPR_VALUE_NUMBER_INIT(1.0), CXPR_VALUE_NUMBER_INIT(2.0)};
    const cxpr_value right[] = {CXPR_VALUE_NUMBER_INIT(2.0), CXPR_VALUE_NUMBER_INIT(4.0), CXPR_VALUE_NUMBER_INIT(8.0)};
    const cxpr_value params[] = {CXPR_VALUE_NUMBER_INIT(1.0), CXPR_VALUE_NUMBER_INIT(0.25)};
    cxpr_value next[3] = {0};
    cxpr_value previous[3] = {0};
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
    assert(next[0].d == 0.0 && next[1].d == 2.25 && next[2].d == 4.5);
    assert(previous[0].d == 0.0 && previous[1].d == 20.0 && previous[2].d == 30.0);
    assert(states[0].previous == 10.0);
    assert(states[1].previous == 2.25 && states[2].previous == 4.5);
    assert(cxpr_bulk_run_range(&descriptor, &view, 3u, 1u) ==
           CXPR_BULK_RANGE_OUT_OF_BOUNDS);
    assert(cxpr_bulk_reset_range(&descriptor, &view, 1u, 1u) == CXPR_BULK_OK);
    assert(states[0].previous == 10.0 && states[1].previous == 0.0 &&
           states[2].previous == 4.5);

    {
        const cxpr_value values[] = {
            CXPR_VALUE_INT64_INIT(INT64_C(9007199254740993)),
            CXPR_VALUE_INT64_INIT(INT64_C(9007199254740994)),
        };
        const cxpr_value increments[] = {CXPR_VALUE_INT64_INIT(1)};
        cxpr_value results[2] = {0};
        const cxpr_bulk_const_column exact_inputs[] = {{values, 1u}};
        cxpr_bulk_column exact_outputs[] = {{results, 1u}};
        const cxpr_bulk_view exact_view = {
            exact_inputs, 1u, increments, 1u, exact_outputs, 1u,
            NULL, 0u, 2u
        };
        assert(cxpr_bulk_validate(&exact_descriptor, &exact_view) == CXPR_BULK_OK);
        assert(cxpr_bulk_run(&exact_descriptor, &exact_view) == CXPR_BULK_OK);
        assert(results[0].i64 == INT64_C(9007199254740994));
        assert(results[1].i64 == INT64_C(9007199254740995));
    }
    puts("bulk execution tests passed");
    return 0;
}
