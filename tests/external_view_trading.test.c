#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/cxpr.h>

#ifndef CXPR_TRADING_VIEW_FIXTURE
#error "CXPR_TRADING_VIEW_FIXTURE must name the authored fixture"
#endif

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

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

static void oracle(const double matrix[4][6], size_t row, double weight,
                   double outputs[4]) {
    const size_t previous = row > 0u ? row - 1u : 0u;
    const double high = matrix[row][1];
    const double low = matrix[row][2];
    const double close = matrix[row][3];
    const double previous_close = matrix[previous][3];
    const double span = high - low;
    const double high_gap = fabs(high - previous_close);
    const double low_gap = fabs(low - previous_close);
    outputs[0] = (high + low + close) / 3.0;
    outputs[1] = fmax(span, fmax(high_gap, low_gap));
    outputs[2] = matrix[row][5];
    outputs[3] = outputs[0] * weight + outputs[2];
}

static void assert_close(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-12);
}

static size_t named_slot(const char* const* names, size_t count,
                         const char* wanted) {
    for (size_t i = 0u; i < count; ++i)
        if (names[i] && strcmp(names[i], wanted) == 0) return i;
    assert(!"required descriptor slot is missing");
    return 0u;
}

static void run_model(const cxpr_model* model,
                      const cxpr_model_compile_options* options,
                      const cxpr_view_requirement* requirement,
                      const cxpr_view_table* table,
                      int64_t row, double weight, const double expected[4]) {
    cxpr_error error = {0};
    cxpr_model_compiled* program = cxpr_model_compile_with_options(
        model, NULL, options, &error);
    cxpr_model_session* session;
    static const char* names[] = {
        "typical", "true_range", "selected_return", "score"};
    if (!program)
        fprintf(stderr, "model view compile failed: %s\n",
                error.message ? error.message : "(null)");
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    assert(cxpr_context_bind_views(cxpr_model_session_context(session),
                                   requirement, table, 1u) == CXPR_VIEW_OK);
    cxpr_context_set_int64(cxpr_model_session_context(session), "row", row);
    cxpr_context_set(cxpr_model_session_context(session), "weight", weight);
    if (!cxpr_model_session_tick(program, session, NULL, &error))
        fprintf(stderr, "model view tick failed: %s\n",
                error.message ? error.message : "(null)");
    assert(error.code == CXPR_OK);
    for (size_t i = 0u; i < 4u; ++i) {
        double actual = 0.0;
        assert(cxpr_model_session_get_number(session, names[i], &actual));
        assert_close(actual, expected[i]);
    }
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
}

static void run_ir(const cxpr_model* model,
                   const cxpr_view_requirement* requirement,
                   const cxpr_view_table* table,
                   int64_t row, double weight, const double expected[4]) {
    static const char* names[] = {
        "typical", "true_range", "selected_return", "score"};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_register_defaults(registry);
    for (size_t i = 0u; i < cxpr_model_function_count(model); ++i) {
        error = cxpr_registry_define_fn(registry, cxpr_model_function_source(model, i));
        assert(error.code == CXPR_OK);
    }
    assert(registry != NULL && context != NULL);
    assert(cxpr_context_bind_views(context, requirement, table, 1u) ==
           CXPR_VIEW_OK);
    cxpr_context_set_int64(context, "row", row);
    cxpr_context_set(context, "weight", weight);
    cxpr_context_set_int64(context, "visits", 0);
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        const cxpr_expr_ast* ast = cxpr_model_binding_expr(model, i);
        const char* name = cxpr_model_binding_name(model, i);
        cxpr_expr_compiled* compiled;
        cxpr_value value = {0};
        if (!ast || !name) continue;
        compiled = cxpr_expr_compile(ast, registry, &error);
        if (!compiled)
            fprintf(stderr, "IR binding %s compile failed: %s\n", name,
                    error.message ? error.message : "(null)");
        assert(compiled != NULL && error.code == CXPR_OK);
        if (!cxpr_expr_compiled_eval(
                compiled, context, registry, &value, &error))
            fprintf(stderr, "IR binding %s failed: %s\n", name,
                    error.message ? error.message : "(null)");
        assert(error.code == CXPR_OK);
        cxpr_context_set_value(context, name, &value);
        cxpr_value_free(&value);
        cxpr_expr_compiled_free(compiled);
    }
    for (size_t i = 0u; i < 4u; ++i) {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(context, names[i], &found);
        assert(found && value.type == CXPR_VALUE_NUMBER);
        assert_close(value.d, expected[i]);
        cxpr_value_free(&value);
    }
    cxpr_context_free(context);
    cxpr_registry_free(registry);
}

static void bind_tree_inputs(cxpr_model_session* session,
                             const cxpr_view_requirement* requirement,
                             const cxpr_view_table* table,
                             int64_t row, double weight) {
    cxpr_context* context = cxpr_model_session_context(session);
    assert(cxpr_context_bind_views(context, requirement, table, 1u) ==
           CXPR_VIEW_OK);
    cxpr_context_set_int64(context, "row", row);
    cxpr_context_set(context, "weight", weight);
}

static void assert_tree_lane(const cxpr_model_session* session,
                             const double expected[4], int64_t visits) {
    static const char* names[] = {
        "typical", "true_range", "selected_return", "score"};
    for (size_t i = 0u; i < 4u; ++i) {
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
                           const cxpr_model_compile_options* options,
                           const cxpr_view_requirement* requirement,
                           const cxpr_view_table* table,
                           const double matrix[4][6]) {
    cxpr_error error = {0};
    cxpr_model_compiled* program = cxpr_model_compile_with_options(
        model, NULL, options, &error);
    cxpr_model_session* lanes[2];
    double expected[4];
    assert(program != NULL);
    lanes[0] = cxpr_model_session_new(program, NULL, &error);
    lanes[1] = cxpr_model_session_new(program, NULL, &error);
    assert(lanes[0] != NULL && lanes[1] != NULL);
    bind_tree_inputs(lanes[0], requirement, table, 2, 0.5);
    bind_tree_inputs(lanes[1], requirement, table, 3, -0.75);
    oracle(matrix, 2u, 0.5, expected);
    assert(cxpr_model_session_tick(program, lanes[0], NULL, &error));
    assert_tree_lane(lanes[0], expected, 1);
    assert(cxpr_model_session_tick(program, lanes[0], NULL, &error));
    assert_tree_lane(lanes[0], expected, 2);
    oracle(matrix, 3u, -0.75, expected);
    assert(cxpr_model_session_tick(program, lanes[1], NULL, &error));
    assert_tree_lane(lanes[1], expected, 1);
    cxpr_model_session_free(lanes[0]);
    cxpr_model_session_free(lanes[1]);
    cxpr_model_compiled_free(program);
}

static void assert_generated(const cxpr_generated_model_descriptor* descriptor,
                             const cxpr_value* outputs,
                             const double expected[4], int64_t visits) {
    static const char* names[] = {
        "typical", "true_range", "selected_return", "score"};
    for (size_t i = 0u; i < 4u; ++i) {
        const cxpr_value value = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, names[i])];
        assert(value.type == CXPR_VALUE_NUMBER);
        assert_close(value.d, expected[i]);
    }
    {
        const cxpr_value value = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, "visits")];
        assert(value.type == CXPR_VALUE_INT64 && value.i64 == visits);
    }
}

int main(void) {
    static const double matrix[4][6] = {
        {100.0, 103.0, 99.0, 102.0, 10.0, 0.00},
        {102.0, 106.0, 101.0, 105.0, 12.0, 0.03},
        {105.0, 107.0, 100.0, 101.0, 15.0, -0.04},
        {101.0, 104.0, 98.0, 103.0, 11.0, 0.02}
    };
    static const double replacement[4][6] = {
        {200.0, 204.0, 198.0, 203.0, 20.0, 0.01},
        {203.0, 209.0, 202.0, 207.0, 21.0, 0.02},
        {207.0, 210.0, 201.0, 205.0, 22.0, -0.01},
        {205.0, 208.0, 200.0, 206.0, 23.0, 0.03}
    };
    static const struct {
        size_t row;
        double weight;
    } cases[] = {{0u, 0.25}, {2u, 0.5}, {3u, -0.75}};
    const cxpr_generated_model_descriptor* descriptor =
        cxpr_generated_model_v1();
    cxpr_view binding = {0};
    cxpr_view_resolved resolved = {0};
    cxpr_view_table table = {&resolved, 1u, 9u};
    cxpr_view_requirement requirement = {0};
    cxpr_model_compile_options tree_options = {
        CXPR_MODEL_BACKEND_AUTO, false, false};
    cxpr_error error = {0};
    char* source = read_source(CXPR_TRADING_VIEW_FIXTURE);
    cxpr_model* model = cxpr_model_parse(source, &error);
    double expected[4];
    cxpr_value inputs[2];
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
    void* states[2];

    assert(source != NULL && model != NULL);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->view_count == 1u && descriptor->view_tick != NULL);
    requirement = descriptor->views[0];
    binding.data = (void*)matrix;
    binding.element_type = CXPR_VALUE_NUMBER;
    binding.rank = 2u;
    binding.shape[0] = 4u;
    binding.shape[1] = 6u;
    binding.strides[0] = 6 * (ptrdiff_t)sizeof(double);
    binding.strides[1] = (ptrdiff_t)sizeof(double);
    binding.capacity_bytes = sizeof(matrix);
    binding.flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS;
    assert(cxpr_views_resolve(
        &requirement, &binding, 1u, &resolved, NULL) == CXPR_VIEW_OK);

    assert(descriptor->output_count == 5u);
    states[0] = calloc(1u, descriptor->state_size());
    states[1] = calloc(1u, descriptor->state_size());
    assert(states[0] != NULL && states[1] != NULL);
    for (size_t case_i = 0u; case_i < sizeof(cases) / sizeof(cases[0]); ++case_i) {
        oracle(matrix, cases[case_i].row, cases[case_i].weight, expected);
        run_model(model, &tree_options, &requirement, &table,
                  (int64_t)cases[case_i].row, cases[case_i].weight, expected);
        run_ir(model, &requirement, &table,
               (int64_t)cases[case_i].row, cases[case_i].weight, expected);
        inputs[0] = cxpr_int64((int64_t)cases[case_i].row);
        inputs[1] = cxpr_num(cases[case_i].weight);
        descriptor->view_tick(states[0], inputs, NULL, outputs, &table);
        assert_generated(descriptor, outputs, expected, (int64_t)case_i + 1);
    }
    run_tree_lanes(model, &tree_options, &requirement, &table, matrix);
    oracle(matrix, 3u, -0.75, expected);
    inputs[0] = cxpr_int64(3);
    inputs[1] = cxpr_num(-0.75);
    descriptor->view_tick(states[1], inputs, NULL, outputs, &table);
    assert_generated(descriptor, outputs, expected, 1);

    binding.data = (void*)replacement;
    binding.capacity_bytes = sizeof(replacement);
    assert(cxpr_views_resolve(
        &requirement, &binding, 1u, &resolved, NULL) == CXPR_VIEW_OK);
    table.generation = 10u;
    oracle(replacement, 1u, 1.25, expected);
    run_model(model, &tree_options, &requirement, &table, 1, 1.25, expected);
    run_ir(model, &requirement, &table, 1, 1.25, expected);
    inputs[0] = cxpr_int64(1);
    inputs[1] = cxpr_num(1.25);
    descriptor->view_tick(states[0], inputs, NULL, outputs, &table);
    assert_generated(descriptor, outputs, expected, 4);

    free(states[0]);
    free(states[1]);
    cxpr_model_free(model);
    free(source);
    puts("external_view_trading: OK");
    return 0;
}
