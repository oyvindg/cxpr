#include <cxpr/cxpr.h>
#include <cxpr/generated.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "large_host_neutral.gen.c"

#ifndef CXPR_SCALE_FIXTURE_DIR
#error "CXPR_SCALE_FIXTURE_DIR must name the standalone scale fixture directory"
#endif

typedef struct scale_loader {
    const char* directory;
} scale_loader;

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

static bool load_import(
    const char* importer_id,
    const char* use_path,
    void* userdata,
    char** out_id,
    char** out_source,
    cxpr_error* error) {
    const scale_loader* loader = (const scale_loader*)userdata;
    char path[1024];
    (void)importer_id;
    (void)error;
    (void)snprintf(
        path, sizeof(path), "%s/%s.cxpr", loader->directory, use_path);
    *out_source = read_file(path);
    *out_id = *out_source ? copy_text(path) : NULL;
    return *out_id && *out_source;
}

static long long now_ns(void) {
    struct timespec time;
    assert(timespec_get(&time, TIME_UTC) == TIME_UTC);
    return (long long)time.tv_sec * 1000000000LL + time.tv_nsec;
}

static void set_inputs(cxpr_context* context, double* inputs, size_t tick) {
    inputs[0] = 50.0 + sin((double)tick * 0.031) * 8.0 + (double)(tick % 7u) * 0.1;
    inputs[1] = 49.0 + cos((double)tick * 0.023) * 6.0;
    inputs[2] = sin((double)tick * 0.017);
    inputs[3] = (double)tick;
    if (!context) return;
    cxpr_context_set(context, "input_a", inputs[0]);
    cxpr_context_set(context, "input_b", inputs[1]);
    cxpr_context_set(context, "control", inputs[2]);
    cxpr_context_set(context, "clock", inputs[3]);
}

static void assert_close(double generated, double reference) {
    const double scale = fmax(1.0, fmax(fabs(generated), fabs(reference)));
    if (isnan(generated) && isnan(reference)) return;
    assert(isfinite(generated));
    assert(isfinite(reference));
    assert(fabs(generated - reference) <= 1e-10 * scale);
}

int main(void) {
    static const char* const numeric_outputs[] = {
        "score_04", "confidence", "selected_value",
        "weighted_energy", "signed_energy",
    };
    const size_t ticks = 8192u;
    const cxpr_generated_model_descriptor* descriptor =
        &cxpr_scale_fixture_tick_descriptor;
    const char* root_path =
        CXPR_SCALE_FIXTURE_DIR "/large_host_neutral.cxpr";
    scale_loader loader = {CXPR_SCALE_FIXTURE_DIR};
    cxpr_error error = {0};
    char* source = read_file(root_path);
    cxpr_model* model;
    cxpr_model_import_bundle* bundle;
    const cxpr_model_import* imports;
    size_t import_count = 0u;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    void* generated_state;
    double params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {0};
    double inputs[4];
    double outputs[7];
    long long reference_start;
    long long reference_ns;
    long long generated_start;
    long long generated_ns;
    volatile double sink = 0.0;
    size_t i;

    assert(source != NULL);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 4u);
    assert(descriptor->output_count == 7u);
    model = cxpr_model_parse(source, &error);
    assert(model != NULL);
    bundle = cxpr_model_import_bundle_build(
        root_path, model, load_import, &loader, &error);
    assert(bundle != NULL);
    imports = cxpr_model_import_bundle_root_imports(bundle, &import_count);
    program = cxpr_model_compile_with_imports(
        model, NULL, imports, import_count, &error);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);
    assert(context != NULL);
    generated_state = calloc(1u, descriptor->state_size());
    assert(generated_state != NULL);
    for (i = 0u; i < descriptor->param_count; ++i) {
        assert(descriptor->param_has_default[i]);
        params[i] = descriptor->param_defaults[i];
    }

    for (i = 0u; i < ticks; ++i) {
        bool reference_bool;
        size_t output;
        set_inputs(context, inputs, i);
        assert(cxpr_model_session_tick(program, session, NULL, &error));
        descriptor->tick(generated_state, inputs, params, outputs);
        if (i < 12u) continue;
        assert(cxpr_model_session_get_bool(session, "active", &reference_bool));
        assert((outputs[0] != 0.0) == reference_bool);
        assert(cxpr_model_session_get_bool(session, "inactive", &reference_bool));
        assert((outputs[1] != 0.0) == reference_bool);
        for (output = 0u; output < 5u; ++output) {
            double reference;
            assert(cxpr_model_session_get_number(
                session, numeric_outputs[output], &reference));
            assert_close(outputs[output + 2u], reference);
        }
    }

    cxpr_model_session_free(session);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session != NULL);
    context = cxpr_model_session_context(session);
    reference_start = now_ns();
    for (i = 0u; i < ticks; ++i) {
        double value;
        set_inputs(context, inputs, i);
        assert(cxpr_model_session_tick(program, session, NULL, &error));
        assert(cxpr_model_session_get_number(session, "score_04", &value));
        if (i >= 12u) sink += value;
    }
    reference_ns = now_ns() - reference_start;

    descriptor->reset(generated_state);
    generated_start = now_ns();
    for (i = 0u; i < ticks; ++i) {
        set_inputs(NULL, inputs, i);
        descriptor->tick(generated_state, inputs, params, outputs);
        if (i >= 12u) sink += outputs[2];
    }
    generated_ns = now_ns() - generated_start;

    printf(
        "cxpr_scale ticks=%zu reference_ns_per_tick=%.2f generated_c_ns_per_tick=%.2f speedup=%.2fx sink=%.6f\n",
        ticks,
        (double)reference_ns / (double)ticks,
        (double)generated_ns / (double)ticks,
        generated_ns > 0 ? (double)reference_ns / (double)generated_ns : 0.0,
        (double)sink);

    free(generated_state);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_import_bundle_free(bundle);
    cxpr_model_free(model);
    free(source);
    return 0;
}
