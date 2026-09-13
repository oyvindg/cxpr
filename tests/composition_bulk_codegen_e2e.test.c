#include <cxpr/cxpr.h>
#include <cxpr/generated.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "composition_fanout.gen.c"

#ifndef CXPR_COMPOSITION_FANOUT_FIXTURE_DIR
#error "CXPR_COMPOSITION_FANOUT_FIXTURE_DIR must name the fixture directory"
#endif

static char* copy_text(const char* text) {
    char* copy = (char*)malloc(strlen(text) + 1u);
    if (copy) strcpy(copy, text);
    return copy;
}

static char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    long size;
    char* source;
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

static bool load_import(const char* importer_id,
                        const char* use_path,
                        void* userdata,
                        char** out_id,
                        char** out_source,
                        cxpr_error* error) {
    const char* directory = (const char*)userdata;
    char path[1024];
    (void)importer_id;
    (void)error;
    (void)snprintf(path, sizeof(path), "%s/%s.cxpr", directory, use_path);
    *out_source = read_file(path);
    *out_id = *out_source ? copy_text(path) : NULL;
    return *out_id && *out_source;
}

static size_t descriptor_name_index(const char* const* names,
                                    size_t count,
                                    const char* wanted) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (strcmp(names[i], wanted) == 0) return i;
    }
    assert(!"generated descriptor is missing an expected name");
    return 0u;
}

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &cxpr_composition_fanout_tick_descriptor;
    const char* fixture_dir = CXPR_COMPOSITION_FANOUT_FIXTURE_DIR;
    const char* root_path = CXPR_COMPOSITION_FANOUT_FIXTURE_DIR "/fanout.cxpr";
    const cxpr_value closes[] = {CXPR_VALUE_NUMBER_INIT(10.0), CXPR_VALUE_NUMBER_INIT(50.0)};
    const size_t element_count = sizeof(closes) / sizeof(closes[0]);
    cxpr_error error = {0};
    char* source = read_file(root_path);
    cxpr_model* model;
    cxpr_model_import_bundle* bundle;
    const cxpr_model_import* imports;
    size_t import_count = 0u;
    cxpr_model_compiled* program;
    cxpr_model_session* sessions[2] = {0};
    double expected[2][3];
    cxpr_value output_storage[3][2] = {{{0}}};
    const cxpr_bulk_const_column input_columns[] = {{closes, 1u}};
    cxpr_bulk_column output_columns[] = {
        {output_storage[0], 1u},
        {output_storage[1], 1u},
        {output_storage[2], 1u},
    };
    void* states;
    cxpr_bulk_view view = {0};
    size_t input_close;
    size_t output_a;
    size_t output_b;
    size_t output_evaluations;
    size_t i;

    assert(source != NULL);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 1u);
    assert(descriptor->output_count == 3u);
    input_close = descriptor_name_index(
        descriptor->input_names, descriptor->input_count, "close");
    output_a = descriptor_name_index(
        descriptor->output_names, descriptor->output_count, "a");
    output_b = descriptor_name_index(
        descriptor->output_names, descriptor->output_count, "b");
    output_evaluations = descriptor_name_index(
        descriptor->output_names, descriptor->output_count, "producer_evaluations");
    assert(input_close == 0u);

    model = cxpr_model_parse(source, &error);
    assert(model != NULL);
    bundle = cxpr_model_import_bundle_build(
        root_path, model, load_import, (void*)fixture_dir, &error);
    assert(bundle != NULL);
    imports = cxpr_model_import_bundle_root_imports(bundle, &import_count);
    program = cxpr_model_compile_with_imports(
        model, NULL, imports, import_count, &error);
    assert(program != NULL);

    for (i = 0u; i < element_count; ++i) {
        cxpr_context* context;
        sessions[i] = cxpr_model_session_new(program, NULL, &error);
        assert(sessions[i] != NULL);
        context = cxpr_model_session_context(sessions[i]);
        assert(context != NULL);
        cxpr_context_set(context, "close", closes[i].d);
        assert(cxpr_model_session_tick(program, sessions[i], NULL, &error));
        assert(cxpr_model_session_get_number(sessions[i], "a", &expected[i][0]));
        assert(cxpr_model_session_get_number(sessions[i], "b", &expected[i][1]));
        assert(cxpr_model_session_get_number(
            sessions[i], "producer_evaluations", &expected[i][2]));
    }

    states = calloc(element_count, descriptor->state_size());
    assert(states != NULL);
    view.element_count = element_count;
    view.inputs = input_columns;
    view.input_count = 1u;
    view.outputs = output_columns;
    view.output_count = 3u;
    view.params = NULL;
    view.param_count = 0u;
    view.states = states;
    view.state_stride = descriptor->state_size();
    assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);

    for (i = 0u; i < element_count; ++i) {
        assert(memcmp(&output_storage[output_a][i].d, &expected[i][0], sizeof(double)) == 0);
        assert(memcmp(&output_storage[output_b][i].d, &expected[i][1], sizeof(double)) == 0);
        assert(memcmp(&output_storage[output_evaluations][i].d,
                      &expected[i][2], sizeof(double)) == 0);
        assert(output_storage[output_evaluations][i].d == 1.0);
    }

    assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);
    for (i = 0u; i < element_count; ++i) {
        double reference;
        cxpr_context_set(cxpr_model_session_context(sessions[i]), "close", closes[i].d);
        assert(cxpr_model_session_tick(program, sessions[i], NULL, &error));
        assert(cxpr_model_session_get_number(
            sessions[i], "producer_evaluations", &reference));
        assert(reference == 2.0);
        assert(memcmp(&output_storage[output_evaluations][i].d,
                      &reference, sizeof(double)) == 0);
    }

    assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);
    for (i = 0u; i < element_count; ++i) {
        double reference;
        cxpr_context_set(cxpr_model_session_context(sessions[i]), "close", closes[i].d);
        assert(cxpr_model_session_tick(program, sessions[i], NULL, &error));
        assert(cxpr_model_session_get_number(
            sessions[i], "producer_evaluations", &reference));
        assert(reference == 3.0);
        assert(memcmp(&output_storage[output_evaluations][i].d,
                      &reference, sizeof(double)) == 0);
    }

    free(states);
    for (i = 0u; i < element_count; ++i) cxpr_model_session_free(sessions[i]);
    cxpr_model_compiled_free(program);
    cxpr_model_import_bundle_free(bundle);
    cxpr_model_free(model);
    free(source);
    puts("composition bulk/codegen fan-out and out-state parity passed");
    return 0;
}
