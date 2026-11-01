/** Fixture-driven IR/generated-C parity for the R6 fold contract. */

#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pathfinding_argmin_argmax.gen.c"
#include "pathfinding_ties.gen.c"

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static uint64_t bits(double value) {
    uint64_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static size_t named_index(const char* const* names, size_t count, const char* name) {
    size_t i;
    for (i = 0u; i < count; ++i)
        if (strcmp(names[i], name) == 0) return i;
    assert(!"missing generated descriptor field");
    return 0u;
}

static char* read_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    snprintf(path, sizeof(path), "%s/fixtures/pathfinding/%s",
             CXPR_TEST_SOURCE_DIR, name);
    file = fopen(path, "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    assert(source && fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);
    return source;
}

static void assert_fixture_parity(
    const char* fixture,
    const cxpr_generated_model_descriptor* descriptor,
    const double* input_numbers,
    size_t input_count) {
    cxpr_error err = {0};
    char* source = read_fixture(fixture);
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    cxpr_value inputs[CXPR_GENERATED_MODEL_MAX_INPUTS] = {{0}};
    cxpr_value outputs[CXPR_GENERATED_MODEL_MAX_OUTPUTS] = {{0}};
    cxpr_value params[CXPR_GENERATED_MODEL_MAX_PARAMS] = {{0}};
    void* state;
    size_t i;

    free(source);
    assert(model && descriptor->input_count == input_count);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session);
    context = cxpr_model_session_context(session);

    for (i = 0u; i < input_count; ++i) {
        inputs[i] = cxpr_num(input_numbers[i]);
        cxpr_context_set(context, descriptor->input_names[i], input_numbers[i]);
    }
    for (i = 0u; i < descriptor->param_count; ++i)
        params[i] = descriptor->param_defaults[i];

    assert(cxpr_model_session_tick(program, session, NULL, &err));
    state = calloc(1u, descriptor->state_size());
    assert(state);
    descriptor->reset(state);
    descriptor->tick(state, inputs, params, outputs);

    for (i = 0u; i < descriptor->output_count; ++i) {
        double interpreted;
        assert(cxpr_model_session_get_number(
            session, descriptor->output_names[i], &interpreted));
        assert(outputs[i].type == CXPR_VALUE_NUMBER);
        assert(bits(outputs[i].d) == bits(interpreted));
    }

    free(state);
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

static void test_basic_fixture(void) {
    const double inputs[] = {3.0, 1.0, 2.0, 1.0};
    assert_fixture_parity("argmin_argmax.cxpr",
                          &pathfinding_argmin_argmax_tick_descriptor,
                          inputs, 4u);
}

static void test_tie_fixture(void) {
    /* Exact product lies just above 1, but rounds to 1 before addition.
     * A contracted fma(mul_a, mul_b, -1) is positive and flips argmin from
     * index 0 to index 1. The required non-contracted result is an exact tie. */
    const double inputs[] = {
        2.0, 3.0,
        0x1.0000000000001p+0,
        0x1.fffffffffffffp-1,
        -0x1p+0,
    };
    cxpr_value generated_inputs[5];
    cxpr_value outputs[3] = {{0}};
    pathfinding_ties_tick_state state = {0};
    size_t near_index;
    size_t i;

    assert_fixture_parity("ties.cxpr", &pathfinding_ties_tick_descriptor,
                          inputs, 5u);
    for (i = 0u; i < 5u; ++i) generated_inputs[i] = cxpr_num(inputs[i]);
    pathfinding_ties_tick(&state, generated_inputs, NULL, outputs);
    near_index = named_index(pathfinding_ties_tick_descriptor.output_names,
                             pathfinding_ties_tick_descriptor.output_count,
                             "near_tie");
    assert(outputs[near_index].d == 0.0);
}

int main(void) {
    test_basic_fixture();
    test_tie_fixture();
    puts("pathfinding fixture IR/generated-C bit parity OK");
    return 0;
}
