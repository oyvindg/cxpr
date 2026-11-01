/**
 * Compare the editable CXPR IR runtime with build-time generated C.
 *
 * A real audio host should keep parsing, compilation, allocation, and model
 * replacement outside its real-time callback. Only descriptor->tick belongs
 * in the fixed-model hot path.
 */

#include <cxpr/cxpr.h>
#include <cxpr/generated.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synth_and_filter.gen.c"

#ifndef CXPR_AUDIO_DSP_SOURCE
#error "CXPR_AUDIO_DSP_SOURCE must name synth_and_filter.cxpr"
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

static void assert_close(double generated, double interpreted) {
    double scale = fmax(1.0, fmax(fabs(generated), fabs(interpreted)));
    assert(isfinite(generated));
    assert(isfinite(interpreted));
    assert(fabs(generated - interpreted) <= 1e-12 * scale);
}

int main(void) {
    const cxpr_generated_model_descriptor* descriptor =
        &synth_and_filter_example_tick_descriptor;
    cxpr_error error = {0};
    char* source = read_source(CXPR_AUDIO_DSP_SOURCE);
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    void* generated_state;
    cxpr_value params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {{0}};
    cxpr_value inputs[2];
    cxpr_value outputs[4];
    size_t sample;
    size_t parameter;
    size_t waveform_parameter = SIZE_MAX;

    assert(source);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 2u);
    assert(descriptor->output_count == 4u);

    model = cxpr_model_parse(source, &error);
    assert(model);
    program = cxpr_model_compile(model, NULL, &error);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &error);
    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);

    generated_state = calloc(1u, descriptor->state_size());
    assert(generated_state);
    for (parameter = 0u; parameter < descriptor->param_count; ++parameter) {
        assert(descriptor->param_has_default[parameter]);
        params[parameter] = descriptor->param_defaults[parameter];
        if (strcmp(descriptor->param_names[parameter], "waveform") == 0) {
            waveform_parameter = parameter;
        }
    }
    assert(waveform_parameter != SIZE_MAX);

    for (sample = 0u; sample < 2048u; ++sample) {
        double interpreted[4];
        double frequency = sample < 1024u ? 220.0 : 440.0;
        double gate = sample < 1536u ? 1.0 : 0.0;
        double waveform = (double)(sample / 512u);

        inputs[0] = cxpr_num(frequency);
        inputs[1] = cxpr_num(gate);
        params[waveform_parameter] = cxpr_num(waveform);
        cxpr_context_set(context, "frequency", frequency);
        cxpr_context_set(context, "gate", gate);
        cxpr_context_set_param(context, "waveform", waveform);
        assert(cxpr_model_session_tick(program, session, NULL, &error));
        assert(cxpr_model_session_get_number(session, "dry", &interpreted[0]));
        assert(cxpr_model_session_get_number(session, "lowpass", &interpreted[1]));
        assert(cxpr_model_session_get_number(session, "highpass", &interpreted[2]));
        assert(cxpr_model_session_get_number(session, "envelope", &interpreted[3]));

        descriptor->tick(generated_state, inputs, params, outputs);
        for (size_t output = 0u; output < 4u; ++output) {
            assert(outputs[output].type == CXPR_VALUE_NUMBER);
            assert_close(outputs[output].d, interpreted[output]);
        }
        assert(outputs[3].d >= 0.0 && outputs[3].d <= 1.0);
    }

    printf("audio_dsp: 2048 samples, 4 oscillators and filter IR/generated-C parity OK\n");
    free(generated_state);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    free(source);
    return 0;
}
