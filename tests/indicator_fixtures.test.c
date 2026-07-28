#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_indicator_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;

    snprintf(path, sizeof(path), "%s/fixtures/indicators/%s",
             CXPR_TEST_SOURCE_DIR, name);
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    rewind(file);

    source = (char*)malloc((size_t)size + 1u);
    assert(source);
    assert(fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);
    return source;
}

static cxpr_model_program* compile_fixture(const char* name,
                                           cxpr_model** out_model) {
    cxpr_error err = {0};
    char* source = read_indicator_fixture(name);
    cxpr_model* model = cxpr_parse_model_source(source, &err);
    cxpr_model_program* program;

    free(source);
    assert(model);
    program = cxpr_compile_model(model, NULL, &err);
    assert(program);
    *out_model = model;
    return program;
}

static void assert_generated_c(cxpr_model_program* program,
                               const char* function_name) {
    cxpr_error err = {0};
    char* code = cxpr_model_program_to_c_tick_function(
        program, "static inline", function_name, &err);

    assert(code);
    free(code);
}

static void test_ema_snapshot(void) {
    static const double inputs[] = {10.0, 14.0, 12.0};
    static const double golden[] = {
        10.0,
        10.380952380952381,
        10.535147392290249,
    };
    cxpr_error err = {0};
    cxpr_model* model;
    cxpr_model_program* program = compile_fixture("ema.cxpr", &model);
    cxpr_model_session* session = cxpr_model_session_new(program, NULL, &err);
    cxpr_context* context;

    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);

    for (size_t i = 0u; i < CXPR_ARRAY_COUNT(inputs); ++i) {
        double actual = 0.0;
        cxpr_context_set(context, "source", inputs[i]);
        assert(cxpr_model_session_tick(program, session, NULL, &err));
        assert(cxpr_model_session_output_number(session, "value", &actual));
        assert(fabs(actual - golden[i]) < 1e-12);
    }

    assert_generated_c(program, "cxpr_test_ema_tick");
    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
}

static void test_true_range_snapshot(void) {
    static const double bars[][3] = {
        {12.0, 9.0, 10.0},
        {15.0, 11.0, 14.0},
        {13.0, 8.0, 9.0},
    };
    static const double golden[] = {3.0, 5.0, 6.0};
    cxpr_error err = {0};
    cxpr_model* model;
    cxpr_model_program* program =
        compile_fixture("true_range.cxpr", &model);
    cxpr_model_session* session = cxpr_model_session_new(program, NULL, &err);
    cxpr_context* context;

    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);

    for (size_t i = 0u; i < CXPR_ARRAY_COUNT(bars); ++i) {
        double actual = 0.0;
        cxpr_context_set(context, "high", bars[i][0]);
        cxpr_context_set(context, "low", bars[i][1]);
        cxpr_context_set(context, "close", bars[i][2]);
        assert(cxpr_model_session_tick(program, session, NULL, &err));
        assert(cxpr_model_session_output_number(session, "value", &actual));
        assert(fabs(actual - golden[i]) < 1e-12);
    }

    assert_generated_c(program, "cxpr_test_true_range_tick");
    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
}

int main(void) {
    test_ema_snapshot();
    test_true_range_snapshot();
    return 0;
}
