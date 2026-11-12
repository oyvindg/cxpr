#include <assert.h>
#include <stdio.h>

#include <cxpr/cxpr.h>

static size_t state_size(void) { return 0u; }

static void view_tick(void* state, const cxpr_value* inputs,
                      const cxpr_value* params, cxpr_value* outputs,
                      const cxpr_view_table* views) {
    cxpr_value loaded = {0};
    const double index[] = {(double)inputs[0].i64};
    (void)state;
    (void)params;
    assert(cxpr_view_load_checked(views, 0u, index, 1u, &loaded) ==
           CXPR_VIEW_OK);
    outputs[0] = loaded;
}

static const cxpr_generated_model_descriptor descriptor = {
    .name = "shared_view_bulk",
    .view_tick = view_tick,
    .state_size = state_size,
    .input_names = {"index"},
    .input_types = {CXPR_GENERATED_VALUE_INT64},
    .input_count = 1u,
    .output_names = {"value"},
    .output_types = {CXPR_GENERATED_VALUE_NUMBER},
    .output_count = 1u,
    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,
    .views = {{
        .name = "samples",
        .slot = 0u,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .max_shape = {8u},
        .min_alignment = _Alignof(double),
        .access = CXPR_VIEW_ACCESS_READ_ONLY,
    }},
    .view_count = 1u,
    .view_abi_version = CXPR_VIEW_ABI_VERSION,
};

int main(void) {
    double samples[] = {10.0, 20.0, 30.0, 40.0};
    const cxpr_value indices[] = {
        CXPR_VALUE_INT64_INIT(3), CXPR_VALUE_INT64_INIT(1),
        CXPR_VALUE_INT64_INIT(2)
    };
    cxpr_value results[3] = {0};
    const cxpr_bulk_const_column inputs[] = {{indices, 1u}};
    cxpr_bulk_column outputs[] = {{results, 1u}};
    const cxpr_bulk_view bulk = {
        .inputs = inputs, .input_count = 1u,
        .outputs = outputs, .output_count = 1u,
        .element_count = 3u,
    };
    const cxpr_view binding = {
        .data = samples,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .shape = {4u},
        .strides = {(ptrdiff_t)sizeof(double)},
        .capacity_bytes = sizeof(samples),
        .flags = CXPR_VIEW_ACCESS_READ_ONLY | CXPR_VIEW_CONTIGUOUS,
    };
    cxpr_view_resolved resolved = {0};
    const cxpr_view_table table = {&resolved, 1u, 42u};

    assert(cxpr_views_resolve(
        descriptor.views, &binding, 1u, &resolved, NULL) ==
        CXPR_VIEW_OK);
    assert(cxpr_bulk_validate(&descriptor, &bulk) ==
           CXPR_BULK_VIEWS_REQUIRED);
    assert(cxpr_bulk_validate_views(&descriptor, &bulk, NULL) ==
           CXPR_BULK_INVALID_VIEWS);
    assert(cxpr_bulk_run_views(&descriptor, &bulk, &table) ==
           CXPR_BULK_OK);
    assert(results[0].d == 40.0 && results[1].d == 20.0 &&
           results[2].d == 30.0);

    {
        cxpr_view_table stale = table;
        stale.count = 0u;
        assert(cxpr_bulk_run_views(&descriptor, &bulk, &stale) ==
               CXPR_BULK_INVALID_VIEWS);
    }
    puts("bulk shared external view tests passed");
    return 0;
}
