#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/cxpr.h>

#ifndef CXPR_AUDIO_VIEW_FIXTURE
#error "CXPR_AUDIO_VIEW_FIXTURE must name the authored fixture"
#endif
#ifndef CXPR_AUDIO_VIEW_GENERATED
#error "CXPR_AUDIO_VIEW_GENERATED must name the generated source"
#endif

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

typedef struct audio_case {
    int64_t sample_index;
    double fraction;
    int64_t pattern;
    int64_t row;
} audio_case;

typedef struct audio_expected {
    double sample;
    int64_t event[5];
} audio_expected;

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

static audio_expected oracle(const double* pcm, size_t pcm_count,
                             const int64_t* tracker, size_t patterns,
                             size_t rows, audio_case test) {
    audio_expected result = {0};
    const size_t left = clamp_index(test.sample_index, pcm_count);
    const size_t right = clamp_index(test.sample_index + 1, pcm_count);
    const int valid = test.pattern >= 0 && (uint64_t)test.pattern < patterns &&
                      test.row >= 0 && (uint64_t)test.row < rows;
    result.sample = pcm[left] + (pcm[right] - pcm[left]) * test.fraction;
    for (size_t field = 0u; field < 4u; ++field) {
        result.event[field] = valid
            ? tracker[((size_t)test.pattern * rows + (size_t)test.row) * 4u + field]
            : -1;
    }
    result.event[4] = result.event[0];
    return result;
}

static void bind_scalars(cxpr_context* context, audio_case test) {
    cxpr_context_set_int64(context, "sample_index", test.sample_index);
    cxpr_context_set(context, "fraction", test.fraction);
    cxpr_context_set_int64(context, "pattern", test.pattern);
    cxpr_context_set_int64(context, "row", test.row);
}

static void assert_close(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-12);
}

static void assert_session(const cxpr_model_session* session,
                           const audio_expected* expected) {
    static const char* names[] = {
        "note", "instrument", "effect", "parameter", "repeated_note"};
    double sample = 0.0;
    assert(cxpr_model_session_get_number(session, "sample", &sample));
    assert_close(sample, expected->sample);
    for (size_t i = 0u; i < 5u; ++i) {
        int64_t actual = 0;
        assert(cxpr_model_session_get_int64(session, names[i], &actual));
        assert(actual == expected->event[i]);
    }
}

static void run_tree(const cxpr_model* model,
                     const cxpr_view_requirement requirements[2],
                     const cxpr_view_table* table, audio_case test,
                     const audio_expected* expected) {
    const cxpr_model_compile_options options = {
        CXPR_MODEL_BACKEND_AUTO, false, false};
    cxpr_error error = {0};
    cxpr_model_compiled* program = cxpr_model_compile_with_options(
        model, NULL, &options, &error);
    cxpr_model_session* session;
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    assert(cxpr_context_bind_views(cxpr_model_session_context(session),
                                   requirements, table, 2u) == CXPR_VIEW_OK);
    bind_scalars(cxpr_model_session_context(session), test);
    if (!cxpr_model_session_tick(program, session, NULL, &error)) {
        fprintf(stderr, "audio tree tick failed: %s\n",
                error.message ? error.message : "(null)");
        assert(false);
    }
    assert_session(session, expected);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
}

static void run_ir(const cxpr_model* model,
                   const cxpr_view_requirement requirements[2],
                   const cxpr_view_table* table, audio_case test,
                   const audio_expected* expected) {
    static const char* names[] = {
        "note", "instrument", "effect", "parameter", "repeated_note"};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_register_defaults(registry);
    for (size_t i = 0u; i < cxpr_model_function_count(model); ++i) {
        error = cxpr_registry_define_fn(registry, cxpr_model_function_source(model, i));
        assert(error.code == CXPR_OK);
    }
    assert(registry && context);
    assert(cxpr_context_bind_views(context, requirements, table, 2u) == CXPR_VIEW_OK);
    bind_scalars(context, test);
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        const cxpr_expr_ast* ast = cxpr_model_binding_expr(model, i);
        const char* name = cxpr_model_binding_name(model, i);
        cxpr_expr_compiled* compiled;
        cxpr_value value = {0};
        if (!ast || !name) continue;
        compiled = cxpr_expr_compile(ast, registry, &error);
        assert(compiled && error.code == CXPR_OK);
        assert(cxpr_expr_compiled_eval(compiled, context, registry, &value, &error));
        cxpr_context_set_value(context, name, &value);
        cxpr_value_free(&value);
        cxpr_expr_compiled_free(compiled);
    }
    {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(context, "sample", &found);
        assert(found && value.type == CXPR_VALUE_NUMBER);
        assert_close(value.d, expected->sample);
    }
    for (size_t i = 0u; i < 5u; ++i) {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(context, names[i], &found);
        assert(found && value.type == CXPR_VALUE_INT64);
        assert(value.i64 == expected->event[i]);
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

static void run_generated(const cxpr_generated_model_descriptor* descriptor,
                          void* state, const cxpr_view_table* table,
                          audio_case test, const audio_expected* expected) {
    static const char* event_names[] = {
        "note", "instrument", "effect", "parameter", "repeated_note"};
    cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
    inputs[named_slot(descriptor->input_names, descriptor->input_count,
                      "sample_index")] = cxpr_int64(test.sample_index);
    inputs[named_slot(descriptor->input_names, descriptor->input_count,
                      "fraction")] = cxpr_num(test.fraction);
    inputs[named_slot(descriptor->input_names, descriptor->input_count,
                      "pattern")] = cxpr_int64(test.pattern);
    inputs[named_slot(descriptor->input_names, descriptor->input_count,
                      "row")] = cxpr_int64(test.row);
    descriptor->view_tick(state, inputs, NULL, outputs, table);
    {
        const cxpr_value sample = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, "sample")];
        assert(sample.type == CXPR_VALUE_NUMBER);
        assert_close(sample.d, expected->sample);
    }
    for (size_t i = 0u; i < 5u; ++i) {
        const cxpr_value value = outputs[named_slot(
            descriptor->output_names, descriptor->output_count, event_names[i])];
        assert(value.type == CXPR_VALUE_INT64);
        assert(value.i64 == expected->event[i]);
    }
}

int main(void) {
    static const double pcm[] = {-1.0, -0.5, 0.25, 1.0, 0.5};
    static const int64_t tracker[2][3][4] = {
        {{48, 1, 0, 0}, {52, 2, 1, 15}, {55, 3, 2, 31}},
        {{60, 4, 3, 47}, {64, 5, 4, 63}, {67, 6, 5, 127}}};
    static const double pcm_b[] = {0.0, 0.5, 1.0};
    static const int64_t tracker_b[1][2][4] = {
        {{72, 8, 6, 10}, {76, 9, 7, 20}}};
    static const audio_case cases[] = {
        {0, 0.0, 0, 0}, {1, 0.5, 0, 1}, {4, 0.75, 1, 2},
        {-3, 0.25, -1, 0}, {99, 0.5, 2, 9}, {1, 0.5, 0, 1}};
    const cxpr_generated_model_descriptor* descriptor = cxpr_generated_model_v1();
    cxpr_view bindings[2] = {{0}};
    cxpr_view_resolved resolved[2] = {{0}};
    cxpr_view_table table = {resolved, 2u, 41u};
    cxpr_error error = {0};
    char* source = read_source(CXPR_AUDIO_VIEW_FIXTURE);
    char* generated = read_source(CXPR_AUDIO_VIEW_GENERATED);
    cxpr_model* model = cxpr_model_parse(source, &error);
    void* state;

    assert(source && generated && model);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->view_count == 2u && descriptor->view_tick != NULL);
    assert(strstr(generated, "_cx_views->views[0u]") != NULL);
    assert(strstr(generated, "_cx_views->views[1u]") != NULL);
    assert(strstr(generated, "malloc(") == NULL);
    assert(strstr(generated, "callback") == NULL);
    bindings[0] = (cxpr_view){(void*)pcm, CXPR_VALUE_NUMBER, 1u, {5u},
        {(ptrdiff_t)sizeof(double)}, sizeof(pcm),
        CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    bindings[1] = (cxpr_view){(void*)tracker, CXPR_VALUE_INT64, 3u, {2u, 3u, 4u},
        {12 * (ptrdiff_t)sizeof(int64_t), 4 * (ptrdiff_t)sizeof(int64_t),
         (ptrdiff_t)sizeof(int64_t)}, sizeof(tracker),
        CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    assert(cxpr_views_resolve(descriptor->views, bindings, 2u, resolved, NULL) ==
           CXPR_VIEW_OK);
    state = calloc(1u, descriptor->state_size());
    assert(state != NULL);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const audio_expected expected = oracle(
            pcm, 5u, &tracker[0][0][0], 2u, 3u, cases[i]);
        run_tree(model, descriptor->views, &table, cases[i], &expected);
        run_ir(model, descriptor->views, &table, cases[i], &expected);
        run_generated(descriptor, state, &table, cases[i], &expected);
    }

    bindings[0] = (cxpr_view){(void*)pcm_b, CXPR_VALUE_NUMBER, 1u, {3u},
        {(ptrdiff_t)sizeof(double)}, sizeof(pcm_b),
        CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    bindings[1] = (cxpr_view){(void*)tracker_b, CXPR_VALUE_INT64, 3u, {1u, 2u, 4u},
        {8 * (ptrdiff_t)sizeof(int64_t), 4 * (ptrdiff_t)sizeof(int64_t),
         (ptrdiff_t)sizeof(int64_t)}, sizeof(tracker_b),
        CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS};
    assert(cxpr_views_resolve(descriptor->views, bindings, 2u, resolved, NULL) ==
           CXPR_VIEW_OK);
    table.generation = 42u;
    {
        const audio_case test = {1, 0.5, 0, 1};
        const audio_expected expected = oracle(
            pcm_b, 3u, &tracker_b[0][0][0], 1u, 2u, test);
        run_tree(model, descriptor->views, &table, test, &expected);
        run_ir(model, descriptor->views, &table, test, &expected);
        run_generated(descriptor, state, &table, test, &expected);
    }
    free(state);
    cxpr_model_free(model);
    free(generated);
    free(source);
    puts("external_view_audio: OK");
    return 0;
}
