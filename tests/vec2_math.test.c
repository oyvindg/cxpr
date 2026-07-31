#include <cxpr/model/model.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    (void)snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    source = (char*)malloc((size_t)size + 1u);
    if (!source || fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[size] = '\0';
    fclose(file);
    return source;
}

static char* join_sources(const char* library, const char* model) {
    size_t library_len = strlen(library);
    size_t model_len = strlen(model);
    char* source = (char*)malloc(library_len + model_len + 2u);
    assert(source != NULL);
    memcpy(source, library, library_len);
    source[library_len] = '\n';
    memcpy(source + library_len + 1u, model, model_len + 1u);
    return source;
}

static void set_vec2(cxpr_context* context, const char* name, double x, double y) {
    static const char* const fields[] = {"x", "y"};
    const double values[] = {x, y};
    cxpr_context_set_fields(context, name, fields, values, 2u);
}

static double output_number(cxpr_model_session* session, const char* name) {
    double value = NAN;
    assert(cxpr_model_session_get_number(session, name, &value));
    return value;
}

static double output_field(cxpr_context* context, const char* object, const char* field) {
    bool found = false;
    cxpr_value value = cxpr_context_get_field(context, object, field, &found);
    assert(found && value.type == CXPR_VALUE_NUMBER);
    return value.d;
}

static void assert_near(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-9);
}

int main(void) {
    cxpr_error error = {0};
    char* library = read_fixture("fixtures/games/vec2.cxpr");
    char* demo = read_fixture("fixtures/games/vec2_math_demo.cxpr");
    char* source;
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    const cxpr_model_compile_options options = {
        .backend = CXPR_MODEL_BACKEND_AUTO,
        .fuse = false,
        .enable_trace = false,
    };

    assert(library != NULL && demo != NULL);
    assert(strstr(library, "delta = left - right") != NULL);
    assert(strstr(library, "left.x * right.x") != NULL);
    assert(strstr(library, "direction - normal *") != NULL);
    assert(strstr(library, "current + delta / remaining * max_delta") != NULL);
    source = join_sources(library, demo);
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "vec2 fixture parse: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_validate_use_files(
        model, CXPR_TEST_SOURCE_DIR "/fixtures/games/vec2_math_demo.cxpr", &error));
    assert(cxpr_model_output_count(model) == 7u);
    program = cxpr_model_compile_with_options(model, NULL, &options, &error);
    if (!program) fprintf(stderr, "vec2 fixture compile: %s\n", error.message);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);

    set_vec2(context, "point_a", 3.0, 4.0);
    set_vec2(context, "point_b", 0.0, 0.0);
    set_vec2(context, "direction", 1.0, -1.0);
    set_vec2(context, "normal", 0.0, 1.0);
    cxpr_context_set(context, "max_delta", 2.0);
    if (!cxpr_model_session_tick(program, session, NULL, &error)) {
        fprintf(stderr, "vec2 fixture tick: %s\n",
                error.message ? error.message : "(null)");
        assert(false);
    }

    assert_near(output_field(context, "delta", "x"), 3.0);
    assert_near(output_field(context, "delta", "y"), 4.0);
    assert_near(output_number(session, "distance_value"), 5.0);
    assert_near(output_number(session, "dot_value"), 0.0);
    assert_near(output_field(context, "normalized", "x"), 0.6);
    assert_near(output_field(context, "normalized", "y"), 0.8);
    assert_near(output_field(context, "reflected", "x"), 1.0);
    assert_near(output_field(context, "reflected", "y"), 1.0);
    assert_near(output_field(context, "moved", "x"), 1.8);
    assert_near(output_field(context, "moved", "y"), 2.4);
    assert_near(output_field(context, "constructed", "x"), 7.0);
    assert_near(output_field(context, "constructed", "y"), 9.0);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    free(source);
    free(demo);
    free(library);
    puts("cxpr Vec2 math fixture tests passed.");
    return 0;
}
