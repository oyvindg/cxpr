#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/cxpr.h>

#ifndef CXPR_SIMULATION_VIEW_FIXTURE
#error "CXPR_SIMULATION_VIEW_FIXTURE must name the authored fixture"
#endif
#ifndef CXPR_SIMULATION_VIEW_GENERATED
#error "CXPR_SIMULATION_VIEW_GENERATED must name the generated source"
#endif

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

typedef struct simulation_case {
    int64_t y;
    int64_t x;
    double fy;
    double fx;
    double bias;
} simulation_case;

static char* read_source(const char* path) {
    FILE* file = fopen(path, "rb");
    long length;
    char* source;
    if (!file) return NULL;
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    source = malloc((size_t)length + 1u);
    assert(source != NULL);
    assert(fread(source, 1u, (size_t)length, file) == (size_t)length);
    source[length] = '\0';
    fclose(file);
    return source;
}

static size_t clamp_index(int64_t value, size_t length) {
    if (value < 0) return 0u;
    if ((uint64_t)value >= (uint64_t)length) return length - 1u;
    return (size_t)value;
}

static void oracle(const double* heights, const int64_t* navigation,
                   size_t rows, size_t columns, simulation_case test,
                   double outputs[3]) {
    const size_t y0 = clamp_index(test.y, rows);
    const size_t x0 = clamp_index(test.x, columns);
    const size_t y1 = clamp_index(test.y + 1, rows);
    const size_t x1 = clamp_index(test.x + 1, columns);
    const double h00 = heights[y0 * columns + x0];
    const double h01 = heights[y0 * columns + x1];
    const double h10 = heights[y1 * columns + x0];
    const double h11 = heights[y1 * columns + x1];
    const double top = h00 + (h01 - h00) * test.fx;
    const double bottom = h10 + (h11 - h10) * test.fx;
    const double height = top + (bottom - top) * test.fy;
    const int valid = test.y >= 0 && (uint64_t)test.y < rows &&
                      test.x >= 0 && (uint64_t)test.x < columns;
    const int64_t cost = valid ? navigation[test.y * (int64_t)columns + test.x]
                               : -1;
    outputs[0] = height;
    outputs[1] = (double)cost;
    outputs[2] = height - (double)cost + test.bias;
}

static void assert_close(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-12);
}

static void bind_scalars(cxpr_context* context, simulation_case test) {
    cxpr_context_set_int64(context, "cell_y", test.y);
    cxpr_context_set_int64(context, "cell_x", test.x);
    cxpr_context_set(context, "fraction_y", test.fy);
    cxpr_context_set(context, "fraction_x", test.fx);
    cxpr_context_set(context, "lane_bias", test.bias);
}

static void assert_session_outputs(const cxpr_model_session* session,
                                   const double expected[3], int64_t visits) {
    static const char* names[] = {"height", "navigation_cost", "movement_score"};
    for (size_t i = 0u; i < 3u; ++i) {
        double actual = 0.0;
        assert(cxpr_model_session_get_number(session, names[i], &actual));
        assert_close(actual, expected[i]);
    }
    {
        int64_t actual = 0;
        assert(cxpr_model_session_get_int64(session, "visits", &actual));
        assert(actual == visits);
    }
}

static void run_tree_lanes(const cxpr_model* model,
                           const cxpr_view_requirement requirements[2],
                           const cxpr_view_table* table, simulation_case test,
                           const double expected[3]) {
    const cxpr_model_compile_options options = {
        CXPR_MODEL_BACKEND_AUTO, false, false};
    cxpr_error error = {0};
    cxpr_model_compiled* program = cxpr_model_compile_with_options(
        model, NULL, &options, &error);
    cxpr_model_session* lanes[2];
    assert(program != NULL);
    for (size_t lane = 0u; lane < 2u; ++lane) {
        lanes[lane] = cxpr_model_session_new(program, NULL, &error);
        assert(lanes[lane] != NULL);
        assert(cxpr_context_bind_views(cxpr_model_session_context(lanes[lane]),
                                       requirements, table, 2u) == CXPR_VIEW_OK);
        bind_scalars(cxpr_model_session_context(lanes[lane]), test);
    }
    assert(cxpr_model_session_tick(program, lanes[0], NULL, &error));
    assert_session_outputs(lanes[0], expected, 1);
    assert(cxpr_model_session_tick(program, lanes[0], NULL, &error));
    assert_session_outputs(lanes[0], expected, 2);
    assert(cxpr_model_session_tick(program, lanes[1], NULL, &error));
    assert_session_outputs(lanes[1], expected, 1);
    cxpr_model_session_free(lanes[0]);
    cxpr_model_session_free(lanes[1]);
    cxpr_model_compiled_free(program);
}

static void run_ir(const cxpr_model* model,
                   const cxpr_view_requirement requirements[2],
                   const cxpr_view_table* table, simulation_case test,
                   const double expected[3]) {
    static const char* names[] = {"height", "navigation_cost", "movement_score"};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_register_defaults(registry);
    for (size_t i = 0u; i < cxpr_model_function_count(model); ++i) {
        error = cxpr_registry_define_fn(registry, cxpr_model_function_source(model, i));
        assert(error.code == CXPR_OK);
    }
    assert(registry != NULL && context != NULL);
    assert(cxpr_context_bind_views(context, requirements, table, 2u) == CXPR_VIEW_OK);
    bind_scalars(context, test);
    cxpr_context_set_int64(context, "visits", 0);
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        const cxpr_expr_ast* ast = cxpr_model_binding_expr(model, i);
        const char* name = cxpr_model_binding_name(model, i);
        cxpr_expr_compiled* compiled;
        cxpr_value value = {0};
        if (!ast || !name) continue;
        compiled = cxpr_expr_compile(ast, registry, &error);
        assert(compiled != NULL && error.code == CXPR_OK);
        assert(cxpr_expr_compiled_eval(compiled, context, registry, &value, &error));
        cxpr_context_set_value(context, name, &value);
        cxpr_value_free(&value);
        cxpr_expr_compiled_free(compiled);
    }
    for (size_t i = 0u; i < 3u; ++i) {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(context, names[i], &found);
        assert(found);
        assert_close(value.type == CXPR_VALUE_INT64 ? (double)value.i64 : value.d,
                     expected[i]);
        cxpr_value_free(&value);
    }
    cxpr_context_free(context);
    cxpr_registry_free(registry);
}

static size_t named_slot(const char* const* names, size_t count, const char* name) {
    for (size_t i = 0u; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    assert(!"missing generated slot");
    return 0u;
}

static void fill_inputs(const cxpr_generated_model_descriptor* descriptor,
                        simulation_case test, cxpr_value* inputs) {
    inputs[named_slot(descriptor->input_names, descriptor->input_count, "cell_y")] =
        cxpr_int64(test.y);
    inputs[named_slot(descriptor->input_names, descriptor->input_count, "cell_x")] =
        cxpr_int64(test.x);
    inputs[named_slot(descriptor->input_names, descriptor->input_count, "fraction_y")] =
        cxpr_num(test.fy);
    inputs[named_slot(descriptor->input_names, descriptor->input_count, "fraction_x")] =
        cxpr_num(test.fx);
    inputs[named_slot(descriptor->input_names, descriptor->input_count, "lane_bias")] =
        cxpr_num(test.bias);
}

static void assert_generated_outputs(
    const cxpr_generated_model_descriptor* descriptor, const cxpr_value* outputs,
    const double expected[3], int64_t visits) {
    static const char* names[] = {"height", "navigation_cost", "movement_score"};
    for (size_t i = 0u; i < 3u; ++i) {
        const cxpr_value value = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, names[i])];
        assert_close(value.type == CXPR_VALUE_INT64 ? (double)value.i64 : value.d,
                     expected[i]);
    }
    {
        const cxpr_value value = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, "visits")];
        assert(value.type == CXPR_VALUE_INT64 && value.i64 == visits);
    }
}

int main(void) {
    static const double heights[3][4] = {
        {0.0, 1.0, 2.0, 3.0},
        {10.0, 11.0, 12.0, 13.0},
        {20.0, 21.0, 22.0, 23.0}};
    static const int64_t navigation[3][4] = {
        {1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
    static const double replacement_height[1][1] = {{42.0}};
    static const int64_t replacement_navigation[1][1] = {{7}};
    static const simulation_case cases[] = {
        {1, 1, 0.0, 0.0, 0.5},
        {0, 0, 0.25, 0.75, -1.0},
        {2, 3, 0.5, 0.5, 2.0},
        {-2, 9, 0.5, 0.5, 0.0}};
    const cxpr_generated_model_descriptor* descriptor = cxpr_generated_model_v1();
    cxpr_view bindings[2] = {{0}};
    cxpr_view_resolved resolved[2] = {{0}};
    cxpr_view_table table = {resolved, 2u, 31u};
    cxpr_error error = {0};
    char* source = read_source(CXPR_SIMULATION_VIEW_FIXTURE);
    char* generated = read_source(CXPR_SIMULATION_VIEW_GENERATED);
    cxpr_model* model = cxpr_model_parse(source, &error);
    void* states[2];

    assert(source && generated && model);
    assert(strstr(generated, "_cx_views->views[0u]") != NULL);
    assert(strstr(generated, "_cx_views->views[1u]") != NULL);
    assert(strstr(generated, "malloc(") == NULL);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->view_count == 2u && descriptor->view_tick != NULL);
    bindings[0] = (cxpr_view){(void*)heights, CXPR_VALUE_NUMBER, 2u,
        {3u, 4u}, {4 * (ptrdiff_t)sizeof(double), (ptrdiff_t)sizeof(double)},
        sizeof(heights), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    bindings[1] = (cxpr_view){(void*)navigation, CXPR_VALUE_INT64, 2u,
        {3u, 4u}, {4 * (ptrdiff_t)sizeof(int64_t), (ptrdiff_t)sizeof(int64_t)},
        sizeof(navigation), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    assert(cxpr_views_resolve(descriptor->views, bindings, 2u, resolved, NULL) ==
           CXPR_VIEW_OK);
    states[0] = calloc(1u, descriptor->state_size());
    states[1] = calloc(1u, descriptor->state_size());
    assert(states[0] && states[1]);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        double expected[3];
        cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
        cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
        oracle(&heights[0][0], &navigation[0][0], 3u, 4u, cases[i], expected);
        run_ir(model, descriptor->views, &table, cases[i], expected);
        fill_inputs(descriptor, cases[i], inputs);
        descriptor->view_tick(states[1], inputs, NULL, outputs, &table);
        assert_generated_outputs(descriptor, outputs, expected, (int64_t)i + 1);
    }
    {
        double expected[3];
        cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
        cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
        oracle(&heights[0][0], &navigation[0][0], 3u, 4u, cases[1], expected);
        run_tree_lanes(model, descriptor->views, &table, cases[1], expected);
        fill_inputs(descriptor, cases[1], inputs);
        descriptor->view_tick(states[0], inputs, NULL, outputs, &table);
        assert_generated_outputs(descriptor, outputs, expected, 1);
    }
    bindings[0] = (cxpr_view){(void*)replacement_height, CXPR_VALUE_NUMBER, 2u,
        {1u, 1u}, {(ptrdiff_t)sizeof(double), (ptrdiff_t)sizeof(double)},
        sizeof(replacement_height), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    bindings[1] = (cxpr_view){(void*)replacement_navigation, CXPR_VALUE_INT64, 2u,
        {1u, 1u}, {(ptrdiff_t)sizeof(int64_t), (ptrdiff_t)sizeof(int64_t)},
        sizeof(replacement_navigation), CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    assert(cxpr_views_resolve(descriptor->views, bindings, 2u, resolved, NULL) ==
           CXPR_VIEW_OK);
    table.generation = 32u;
    {
        const simulation_case test = {0, 0, 0.75, 0.25, 3.0};
        double expected[3];
        cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
        cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
        oracle(&replacement_height[0][0], &replacement_navigation[0][0],
               1u, 1u, test, expected);
        run_ir(model, descriptor->views, &table, test, expected);
        fill_inputs(descriptor, test, inputs);
        descriptor->view_tick(states[0], inputs, NULL, outputs, &table);
        assert_generated_outputs(descriptor, outputs, expected, 2);
    }
    free(states[0]);
    free(states[1]);
    cxpr_model_free(model);
    free(generated);
    free(source);
    puts("external_view_simulation: OK");
    return 0;
}
