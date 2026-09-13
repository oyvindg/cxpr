/**
 * Host-neutral parity runner for one stateful, multi-output CXPR model.
 *
 * The generated source is produced by cxpr_model_codegen at build time. This
 * file deliberately includes it so the example can consume its static
 * descriptor without introducing a platform-specific plugin loader.
 */

#include <cxpr/cxpr.h>
#include <cxpr/generated.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiled_strategy.gen.c"

#ifndef CXPR_COMPILED_STRATEGY_SOURCE
#error "CXPR_COMPILED_STRATEGY_SOURCE must name strategy.cxpr"
#endif

static char* read_source(const char* path) {
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

static void assert_close(double generated, double reference) {
    const double scale = fmax(1.0, fmax(fabs(generated), fabs(reference)));
    assert(isfinite(generated));
    assert(isfinite(reference));
    assert(fabs(generated - reference) <= 1e-12 * scale);
}

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &compiled_strategy_example_tick_descriptor;
    cxpr_error error = {0};
    char* source = read_source(CXPR_COMPILED_STRATEGY_SOURCE);
    cxpr_model* model;
    cxpr_model_compiled* compiled;
    cxpr_model_session* session;
    cxpr_context* context;
    void* generated_state;
    double params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {0};
    double inputs[3];
    double outputs[4];
    double first_outputs[4];
    size_t i;

    assert(source);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 3u);
    assert(descriptor->output_count == 4u);
    assert(descriptor->param_count == 1u);
    assert(descriptor->reset);

    model = cxpr_model_parse(source, &error);
    assert(model);
    compiled = cxpr_model_compile(model, NULL, &error);
    assert(compiled);
    session = cxpr_model_session_new(compiled, NULL, &error);
    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);

    generated_state = calloc(1u, descriptor->state_size());
    assert(generated_state);
    for (i = 0u; i < descriptor->param_count; ++i) {
        assert(descriptor->param_has_default[i]);
        params[i] = descriptor->param_defaults[i];
    }

    for (i = 0u; i < 512u; ++i) {
        bool crossed;
        bool active;
        double delta;
        double total;

        inputs[0] = sin((double)i * 0.071) + (double)(i % 11u) * 0.03;
        inputs[1] = cos((double)i * 0.047) * 0.7;
        inputs[2] = (double)i;

        cxpr_context_set(context, "input_a", inputs[0]);
        cxpr_context_set(context, "input_b", inputs[1]);
        cxpr_context_set(context, "clock", inputs[2]);
        assert(cxpr_model_session_tick(compiled, session, NULL, &error));

        descriptor->tick(generated_state, inputs, params, outputs);
        if (i == 0u) {
            for (size_t output = 0u; output < 4u; ++output) {
                first_outputs[output] = outputs[output];
            }
        }
        assert(cxpr_model_session_get_bool(session, "crossed", &crossed));
        assert(cxpr_model_session_get_bool(session, "active", &active));
        assert(cxpr_model_session_get_number(session, "delta", &delta));
        assert(cxpr_model_session_get_number(session, "total", &total));

        assert((outputs[0] != 0.0) == crossed);
        assert((outputs[1] != 0.0) == active);
        assert_close(outputs[2], delta);
        assert_close(outputs[3], total);
    }

    descriptor->reset(generated_state);
    inputs[0] = sin(0.0);
    inputs[1] = cos(0.0) * 0.7;
    inputs[2] = 0.0;
    descriptor->tick(generated_state, inputs, params, outputs);
    for (i = 0u; i < 4u; ++i) assert_close(outputs[i], first_outputs[i]);

    printf("compiled_strategy: 512 ticks, 4 outputs, engine/generated-C parity OK\n");
    free(generated_state);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(compiled);
    cxpr_model_free(model);
    free(source);
    return 0;
}
