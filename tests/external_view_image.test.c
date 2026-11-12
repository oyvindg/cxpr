#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/cxpr.h>

#ifndef CXPR_IMAGE_VIEW_FIXTURE
#error "CXPR_IMAGE_VIEW_FIXTURE must name the authored fixture"
#endif
#ifndef CXPR_IMAGE_VIEW_GENERATED
#error "CXPR_IMAGE_VIEW_GENERATED must name the generated C source"
#endif

const cxpr_generated_model_descriptor* cxpr_generated_model_v1(void);

typedef struct image_case {
    int64_t y;
    int64_t x;
    int64_t channel;
} image_case;

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

static void oracle(const double image[4][5][2], const double kernel[3][3],
                   image_case test, double outputs[2]) {
    double sum = 0.0;
    const size_t channel = clamp_index(test.channel, 2u);
    for (int64_t ky = 0; ky < 3; ++ky) {
        for (int64_t kx = 0; kx < 3; ++kx) {
            const size_t y = clamp_index(test.y + ky - 1, 4u);
            const size_t x = clamp_index(test.x + kx - 1, 5u);
            sum += image[y][x][channel] * kernel[ky][kx];
        }
    }
    outputs[0] = sum;
    outputs[1] = test.y >= 0 && test.y < 4 &&
                 test.x >= 0 && test.x < 5 &&
                 test.channel >= 0 && test.channel < 2
        ? image[test.y][test.x][test.channel] : -777.0;
}

static void assert_close(double actual, double expected) {
    assert(fabs(actual - expected) < 1e-12);
}

static void bind_scalars(cxpr_context* context, image_case test) {
    cxpr_context_set_int64(context, "y", test.y);
    cxpr_context_set_int64(context, "x", test.x);
    cxpr_context_set_int64(context, "channel", test.channel);
}

static void run_tree(const cxpr_model* model,
                     const cxpr_view_requirement requirements[2],
                     const cxpr_view_table* table, image_case test,
                     const double expected[2]) {
    static const char* names[] = {"convolved", "center"};
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
    assert(cxpr_model_session_tick(program, session, NULL, &error));
    for (size_t i = 0u; i < 2u; ++i) {
        double actual = 0.0;
        assert(cxpr_model_session_get_number(session, names[i], &actual));
        assert_close(actual, expected[i]);
    }
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
}

static void run_ir(const cxpr_model* model,
                   const cxpr_view_requirement requirements[2],
                   const cxpr_view_table* table, image_case test,
                   const double expected[2]) {
    static const char* names[] = {"convolved", "center"};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_register_defaults(registry);
    for (size_t i = 0u; i < cxpr_model_function_count(model); ++i) {
        error = cxpr_registry_define_fn(registry, cxpr_model_function_source(model, i));
        assert(error.code == CXPR_OK);
    }
    assert(registry != NULL && context != NULL);
    assert(cxpr_context_bind_views(context, requirements, table, 2u) ==
           CXPR_VIEW_OK);
    bind_scalars(context, test);
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        const cxpr_expr_ast* ast = cxpr_model_binding_expr(model, i);
        const char* name = cxpr_model_binding_name(model, i);
        cxpr_expr_compiled* compiled;
        cxpr_value value = {0};
        if (!ast || !name) continue;
        compiled = cxpr_expr_compile(ast, registry, &error);
        assert(compiled != NULL && error.code == CXPR_OK);
        assert(cxpr_expr_compiled_eval(
            compiled, context, registry, &value, &error));
        cxpr_context_set_value(context, name, &value);
        cxpr_value_free(&value);
        cxpr_expr_compiled_free(compiled);
    }
    for (size_t i = 0u; i < 2u; ++i) {
        bool found = false;
        cxpr_value value = cxpr_context_get_typed(context, names[i], &found);
        assert(found && value.type == CXPR_VALUE_NUMBER);
        assert_close(value.d, expected[i]);
        cxpr_value_free(&value);
    }
    cxpr_context_free(context);
    cxpr_registry_free(registry);
}

int main(void) {
    double image[4][5][2];
    const double kernel[3][3] = {
        {0.0, -1.0, 0.0},
        {-1.0, 5.0, -1.0},
        {0.0, -1.0, 0.0}
    };
    static const image_case cases[] = {
        {2, 2, 0}, {2, 2, 1},
        {0, 2, 0}, {3, 2, 1}, {2, 0, 0}, {2, 4, 1},
        {0, 0, 0}, {0, 4, 1}, {3, 0, 1}, {3, 4, 0},
        {1, 1, 7}
    };
    const cxpr_generated_model_descriptor* descriptor =
        cxpr_generated_model_v1();
    cxpr_view bindings[2] = {{0}};
    cxpr_view_resolved resolved[2] = {{0}};
    cxpr_view_table table = {resolved, 2u, 17u};
    cxpr_error error = {0};
    char* source = read_source(CXPR_IMAGE_VIEW_FIXTURE);
    char* generated_source = read_source(CXPR_IMAGE_VIEW_GENERATED);
    cxpr_model* model = cxpr_model_parse(source, &error);
    void* state;

    for (size_t y = 0u; y < 4u; ++y)
        for (size_t x = 0u; x < 5u; ++x)
            for (size_t channel = 0u; channel < 2u; ++channel)
                image[y][x][channel] =
                    (double)(100u * channel + 10u * y + x);

    assert(source != NULL && generated_source != NULL && model != NULL);
    assert(strstr(generated_source, "_cx_views->views[0u]") != NULL);
    assert(strstr(generated_source, "_cx_views->views[1u]") != NULL);
    assert(strstr(generated_source, "malloc(") == NULL);
    assert(strstr(generated_source, "callback") == NULL);
    assert(cxpr_generated_model_descriptor_abi_valid(descriptor));
    assert(descriptor->view_count == 2u && descriptor->view_tick != NULL);
    assert(descriptor->views[0].rank == 3u);
    assert(descriptor->views[1].rank == 2u);

    bindings[0].data = image;
    bindings[0].element_type = CXPR_VALUE_NUMBER;
    bindings[0].rank = 3u;
    bindings[0].shape[0] = 4u;
    bindings[0].shape[1] = 5u;
    bindings[0].shape[2] = 2u;
    bindings[0].strides[0] = 10 * (ptrdiff_t)sizeof(double);
    bindings[0].strides[1] = 2 * (ptrdiff_t)sizeof(double);
    bindings[0].strides[2] = (ptrdiff_t)sizeof(double);
    bindings[0].capacity_bytes = sizeof(image);
    bindings[0].flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS;
    bindings[1].data = (void*)kernel;
    bindings[1].element_type = CXPR_VALUE_NUMBER;
    bindings[1].rank = 2u;
    bindings[1].shape[0] = 3u;
    bindings[1].shape[1] = 3u;
    bindings[1].strides[0] = 3 * (ptrdiff_t)sizeof(double);
    bindings[1].strides[1] = (ptrdiff_t)sizeof(double);
    bindings[1].capacity_bytes = sizeof(kernel);
    bindings[1].flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS;
    assert(cxpr_views_resolve(
        descriptor->views, bindings, 2u, resolved, NULL) == CXPR_VIEW_OK);

    state = calloc(1u, descriptor->state_size());
    assert(state != NULL);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        double expected[2];
        cxpr_value inputs[3] = {
            cxpr_int64(cases[i].y), cxpr_int64(cases[i].x),
            cxpr_int64(cases[i].channel)};
        cxpr_value outputs[2] = {0};
        oracle(image, kernel, cases[i], expected);
        run_tree(model, descriptor->views, &table, cases[i], expected);
        run_ir(model, descriptor->views, &table, cases[i], expected);
        descriptor->view_tick(state, inputs, NULL, outputs, &table);
        for (size_t output = 0u; output < 2u; ++output) {
            assert(outputs[output].type == CXPR_VALUE_NUMBER);
            assert_close(outputs[output].d, expected[output]);
        }
    }

    free(state);
    cxpr_model_free(model);
    free(generated_source);
    free(source);
    puts("external_view_image: OK");
    return 0;
}
