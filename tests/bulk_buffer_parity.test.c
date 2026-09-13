#include <cxpr/bulk.h>
#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rolling_buffer.gen.c"

enum { ELEMENT_COUNT = 3, TICK_COUNT = 7 };

static char* read_fixture(void) {
    const char* path = CXPR_TEST_SOURCE_DIR "/fixtures/bulk/rolling_buffer.cxpr";
    FILE* file = fopen(path, "rb");
    long size;
    char* text;
    assert(file && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0 && fseek(file, 0, SEEK_SET) == 0);
    text = (char*)malloc((size_t)size + 1u);
    assert(text && fread(text, 1u, (size_t)size, file) == (size_t)size);
    text[size] = '\0';
    fclose(file);
    return text;
}

static size_t named_index(const char* const* names, size_t count, const char* name) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    assert(!"missing generated name");
    return 0u;
}

static void assert_same(double actual, double expected) {
    if (isnan(expected)) assert(isnan(actual));
    else assert(actual == expected);
}

int main(void) {
    static const double streams[ELEMENT_COUNT][TICK_COUNT] = {
        {1, 2, 3, 4, 5, 6, 7},
        {101, 103, 107, 109, 113, 127, 131},
        {-5, -4, -2, 1, 5, 10, 16},
    };
    const cxpr_generated_model_descriptor* descriptor = &rolling_buffer_tick_descriptor;
    cxpr_error err = {0};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* sessions[ELEMENT_COUNT];
    cxpr_bulk_const_column inputs[1];
    cxpr_bulk_column outputs[3];
    double input_values[ELEMENT_COUNT];
    double output_values[3][ELEMENT_COUNT];
    void* states;
    size_t state_size;
    char* source = read_fixture();
    size_t element;
    size_t tick;

    assert(registry);
    cxpr_register_defaults(registry);
    model = cxpr_model_parse(source, &err);
    assert(model && err.code == CXPR_OK && cxpr_model_validate(model, &err));
    assert(err.code == CXPR_OK);
    program = cxpr_model_compile(model, registry, &err);
    assert(program && err.code == CXPR_OK);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->input_count == 1u && descriptor->output_count == 3u);

    state_size = descriptor->state_size();
    assert(state_size > 0u);
    states = malloc(ELEMENT_COUNT * state_size);
    assert(states);
    memset(states, 0xa5, ELEMENT_COUNT * state_size);

    inputs[0] = (cxpr_bulk_const_column){input_values, 1u};
    outputs[named_index(descriptor->output_names, 3u, "newest")] =
        (cxpr_bulk_column){output_values[0], 1u};
    outputs[named_index(descriptor->output_names, 3u, "oldest")] =
        (cxpr_bulk_column){output_values[1], 1u};
    outputs[named_index(descriptor->output_names, 3u, "window_sum")] =
        (cxpr_bulk_column){output_values[2], 1u};
    {
        const cxpr_bulk_view view = {
            inputs, 1u, NULL, 0u, outputs, 3u,
            states, state_size, ELEMENT_COUNT};
        /* Validation cannot identify dirty storage; reset is the documented contract. */
        assert(cxpr_bulk_validate(descriptor, &view) == CXPR_BULK_OK);
        assert(cxpr_bulk_reset(descriptor, &view) == CXPR_BULK_OK);

        for (element = 0u; element < ELEMENT_COUNT; ++element) {
            sessions[element] = cxpr_model_session_new(program, registry, &err);
            assert(sessions[element]);
        }
        for (tick = 0u; tick < TICK_COUNT; ++tick) {
            for (element = 0u; element < ELEMENT_COUNT; ++element)
                input_values[element] = streams[element][tick];
            assert(cxpr_bulk_run(descriptor, &view) == CXPR_BULK_OK);
            for (element = 0u; element < ELEMENT_COUNT; ++element) {
                double expected[3];
                cxpr_context_set(cxpr_model_session_context(sessions[element]),
                                 "sample", streams[element][tick]);
                assert(cxpr_model_session_tick(program, sessions[element], registry, &err));
                assert(cxpr_model_session_get_number(sessions[element], "newest", &expected[0]));
                assert(cxpr_model_session_get_number(sessions[element], "oldest", &expected[1]));
                assert(cxpr_model_session_get_number(sessions[element], "window_sum", &expected[2]));
                assert_same(output_values[0][element], expected[0]);
                assert_same(output_values[1][element], expected[1]);
                assert_same(output_values[2][element], expected[2]);
            }
        }
    }

    assert(output_values[0][0] != output_values[0][1]);
    assert(output_values[0][1] != output_values[0][2]);
    for (element = 0u; element < ELEMENT_COUNT; ++element)
        cxpr_model_session_free(sessions[element]);
    free(states);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
    free(source);
    puts("bulk buffer parity: session and independent generated states match");
    return 0;
}
