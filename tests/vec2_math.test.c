#include <cxpr/model/imports.h>

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

static char* duplicate_text(const char* text) {
    size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static bool load_vec2_import(const char* importer_id,
                             const char* use_path,
                             void* userdata,
                             char** out_id,
                             char** out_source,
                             cxpr_error* error) {
    const char* library = (const char*)userdata;
    (void)importer_id;
    (void)error;
    *out_id = NULL;
    *out_source = NULL;
    if (strcmp(use_path, "geometry2d") != 0) return false;
    *out_id = duplicate_text("geometry2d");
    *out_source = duplicate_text(library);
    if (*out_id && *out_source) return true;
    free(*out_id);
    free(*out_source);
    *out_id = NULL;
    *out_source = NULL;
    return false;
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
    char* library = read_fixture("fixtures/games/geometry2d.cxpr");
    char* demo = read_fixture("fixtures/games/vec2_math_demo.cxpr");
    cxpr_model* model;
    cxpr_model_import_bundle* bundle;
    const cxpr_model_import* imports;
    size_t import_count = 0u;
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
    model = cxpr_model_parse(demo, &error);
    if (!model) fprintf(stderr, "vec2 fixture parse: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_validate_use_files(
        model, CXPR_TEST_SOURCE_DIR "/fixtures/games/vec2_math_demo.cxpr", &error));
    assert(cxpr_model_output_count(model) == 6u);
    bundle = cxpr_model_import_bundle_build(
        "vec2_math_demo", model, load_vec2_import, library, &error);
    if (!bundle) fprintf(stderr, "vec2 import bundle: %s\n", error.message);
    assert(bundle != NULL);
    imports = cxpr_model_import_bundle_root_imports(bundle, &import_count);
    assert(imports != NULL && import_count == 1u);
    program = cxpr_model_compile_full(
        model, NULL, imports, import_count, &options, &error);
    if (!program) fprintf(stderr, "vec2 fixture compile: %s\n", error.message);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);

    cxpr_context_set(context, "point_a_x", 3.0);
    cxpr_context_set(context, "point_a_y", 4.0);
    cxpr_context_set(context, "direction_x", 1.0);
    cxpr_context_set(context, "direction_y", -1.0);
    cxpr_context_set(context, "normal_x", 0.0);
    cxpr_context_set(context, "normal_y", 1.0);
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
    assert_near(output_field(context, "constructed", "x"), 7.0);
    assert_near(output_field(context, "constructed", "y"), 9.0);

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_import_bundle_free(bundle);
    cxpr_model_free(model);
    free(demo);
    free(library);
    puts("cxpr Vec2 math fixture tests passed.");
    return 0;
}
