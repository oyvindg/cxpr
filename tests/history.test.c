#include <cxpr/cxpr.h>

#include "cxpr_test_internal.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    double close;
    double high;
} bar;

typedef struct {
    const bar* bars;
    size_t count;
    int64_t cursor;
} dynamic_history;

static bool dynamic_history_view(
    void* userdata, const char* name,
    cxpr_history_numeric_source* out, int64_t* cursor) {
    dynamic_history* view = (dynamic_history*)userdata;
    const double* base;
    if (!view || !view->bars || !out || !cursor) return false;
    if (strcmp(name, "close") == 0) base = &view->bars[0].close;
    else if (strcmp(name, "high") == 0) base = &view->bars[0].high;
    else return false;
    *out = (cxpr_history_numeric_source){name, base, sizeof(bar), view->count};
    *cursor = view->cursor;
    return true;
}

static cxpr_value evaluate(const char* source, cxpr_context* context,
                           cxpr_registry* registry, bool compiled,
                           cxpr_error* error) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, error);
    cxpr_value result = cxpr_null();
    assert(parser && ast);
    if (compiled) {
        cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, error);
        assert(program != NULL);
        result = cxpr_test_eval_program(program, context, registry, error);
        cxpr_expr_compiled_free(program);
    } else if (!cxpr_eval_ast(ast, context, registry, &result, error)) {
        result = cxpr_num(NAN);
    }
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return result;
}

static void test_contiguous_dynamic_offsets_and_policies(void) {
    static const double values[] = {10.0, 20.0, 30.0};
    int64_t cursor = 2;
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_value result;
    assert(context && registry);
    assert(cxpr_register_history_contiguous_numbers(
        registry, "close", values, 3u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    cxpr_context_set(context, "offset", 1.0);

    result = evaluate("close[offset]", context, registry, false, &error);
    assert(error.code == CXPR_OK && result.type == CXPR_VALUE_NUMBER && result.d == 20.0);
    result = evaluate("close[offset]", context, registry, true, &error);
    assert(error.code == CXPR_OK && result.type == CXPR_VALUE_NUMBER && result.d == 20.0);

    cursor = 0;
    result = evaluate("close[1]", context, registry, false, &error);
    assert(isnan(result.d));
    assert(error.code == CXPR_ERR_INDEX_OUT_OF_RANGE);
    assert(strcmp(error.message, "History offset is out of range") == 0);
    cxpr_registry_free(registry);

    registry = cxpr_registry_new();
    assert(cxpr_register_history_contiguous_numbers(
        registry, "close", values, 3u, &cursor, CXPR_HISTORY_BOUNDS_CLAMP_FIRST));
    result = evaluate("close[2]", context, registry, true, &error);
    assert(error.code == CXPR_OK && result.d == 10.0);

    cxpr_registry_free(registry);
    cxpr_context_free(context);
}

static void test_strided_multiple_sources_and_isolated_cursors(void) {
    static const bar bars[] = {{10.0, 11.0}, {20.0, 22.0}, {30.0, 33.0}};
    const cxpr_history_numeric_source sources[] = {
        {"close", &bars[0].close, sizeof(bar), 3u},
        {"high", &bars[0].high, sizeof(bar), 3u},
    };
    int64_t cursor_a = 2;
    int64_t cursor_b = 1;
    cxpr_registry* registry_a = cxpr_registry_new();
    cxpr_registry* registry_b = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_value result;
    assert(registry_a && registry_b);
    assert(cxpr_register_history_numeric_sources(
        registry_a, sources, 2u, &cursor_a, CXPR_HISTORY_BOUNDS_ERROR));
    assert(cxpr_register_history_numeric_sources(
        registry_b, sources, 2u, &cursor_b, CXPR_HISTORY_BOUNDS_ERROR));

    result = evaluate("close[1] + high[2]", NULL, registry_a, true, &error);
    assert(error.code == CXPR_OK && result.d == 31.0);
    result = evaluate("close[1] + high[0]", NULL, registry_b, false, &error);
    assert(error.code == CXPR_OK && result.d == 32.0);
    result = evaluate("close[0]", NULL, registry_a, false, &error);
    assert(error.code == CXPR_OK && result.d == 30.0);
    result = evaluate("[1, 2][1] + close[1]", NULL, registry_a, false, &error);
    assert(error.code == CXPR_OK && result.d == 22.0);
    result = evaluate("[1, 2][1] + close[1]", NULL, registry_a, true, &error);
    assert(error.code == CXPR_OK && result.d == 22.0);

    cxpr_registry_free(registry_b);
    cxpr_registry_free(registry_a);
}

static void test_invalid_dynamic_offsets(void) {
    static const double values[] = {10.0};
    int64_t cursor = 0;
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    const double invalid[] = {-1.0, 0.5, NAN, INFINITY, 1e30};
    assert(context && registry);
    assert(cxpr_register_history_contiguous_numbers(
        registry, "close", values, 1u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        cxpr_error error = {0};
        cxpr_value result;
        cxpr_context_set(context, "offset", invalid[i]);
        result = evaluate("close[offset]", context, registry, i % 2u != 0u, &error);
        assert(isnan(result.d));
        assert(error.code == CXPR_ERR_INVALID_INDEX);
        assert(strcmp(error.message,
                      "Index must be a finite non-negative integer") == 0);
    }
    cxpr_registry_free(registry);
    cxpr_context_free(context);
}

static void test_compound_expression_uses_immutable_shifted_context(void) {
    static const bar bars[] = {{10.0, 11.0}, {20.0, 22.0}, {30.0, 33.0}};
    const cxpr_history_numeric_source sources[] = {
        {"close", &bars[0].close, sizeof(bar), 3u},
        {"high", &bars[0].high, sizeof(bar), 3u},
    };
    int64_t cursor = 2;
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_value result;
    assert(context && registry);
    cxpr_context_set(context, "bias", 5.0);
    assert(cxpr_register_history_numeric_sources(
        registry, sources, 2u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));

    result = evaluate("(close + high)[1]", context, registry, false, &error);
    assert(error.code == CXPR_OK && result.d == 42.0 && cursor == 2);
    result = evaluate("(close + high + bias)[2]", context, registry, true, &error);
    assert(error.code == CXPR_OK && result.d == 26.0 && cursor == 2);
    result = evaluate("(close + high)[1][1]", context, registry, false, &error);
    assert(error.code == CXPR_OK && result.d == 21.0 && cursor == 2);
    result = evaluate("(close + high)[1][1]", context, registry, true, &error);
    assert(error.code == CXPR_OK && result.d == 21.0 && cursor == 2);

    cxpr_registry_free(registry);
    cxpr_context_free(context);
}

static void test_dynamic_provider_rebinds_without_copying(void) {
    static const bar first[] = {{10.0, 11.0}, {20.0, 22.0}};
    static const bar second[] = {{40.0, 44.0}, {50.0, 55.0}, {60.0, 66.0}};
    static const char* const names[] = {"close", "high"};
    dynamic_history view = {first, 2u, 1};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_value result;
    assert(registry);
    assert(cxpr_register_history_numeric_provider(
        registry, names, 2u, dynamic_history_view, &view, NULL,
        CXPR_HISTORY_BOUNDS_CLAMP_FIRST));
    result = evaluate("(close + high)[1]", NULL, registry, true, &error);
    assert(error.code == CXPR_OK && result.d == 21.0);
    view.bars = second;
    view.count = 3u;
    view.cursor = 2;
    result = evaluate("close[1] + high[0]", NULL, registry, false, &error);
    assert(error.code == CXPR_OK && result.d == 116.0);
    view.cursor = 0;
    result = evaluate("close[99]", NULL, registry, true, &error);
    assert(error.code == CXPR_OK && result.d == 40.0);
    cxpr_registry_free(registry);
}

static void test_malformed_and_overflowing_registration(void) {
    static const double values[] = {10.0};
    int64_t cursor = 0;
    cxpr_registry* registry = cxpr_registry_new();
    const cxpr_history_numeric_source duplicate[] = {
        {"close", values, sizeof(double), 1u},
        {"close", values, sizeof(double), 1u},
    };
    const cxpr_history_numeric_source empty_name = {"", values, sizeof(double), 1u};
    const cxpr_history_numeric_source empty_data = {"close", values, sizeof(double), 0u};
    const cxpr_history_numeric_source overflowing = {
        "close", values, SIZE_MAX, 2u,
    };
    assert(registry);
    assert(!cxpr_register_history_numeric_sources(
        registry, NULL, 0u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_numeric_sources(
        registry, duplicate, 2u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_numeric_sources(
        registry, &empty_name, 1u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_numeric_sources(
        registry, &empty_data, 1u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_numeric_sources(
        registry, &overflowing, 1u, &cursor, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_contiguous_numbers(
        registry, "close", values, 1u, NULL, CXPR_HISTORY_BOUNDS_ERROR));
    assert(!cxpr_register_history_contiguous_numbers(
        registry, "close", values, 1u, &cursor,
        (cxpr_history_bounds_policy)99));
    cxpr_registry_free(registry);
}

typedef struct {
    const double* values;
    int64_t cursor;
    double expected;
    bool compiled;
} history_thread_case;

static void* evaluate_shared_history(void* userdata) {
    history_thread_case* test = (history_thread_case*)userdata;
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    assert(context && registry);
    assert(cxpr_register_history_contiguous_numbers(
        registry, "close", test->values, 4u, &test->cursor,
        CXPR_HISTORY_BOUNDS_ERROR));
    for (size_t i = 0u; i < 500u; ++i) {
        cxpr_error error = {0};
        cxpr_value result = evaluate(
            "(close * 2)[1]", context, registry, test->compiled, &error);
        assert(error.code == CXPR_OK && result.d == test->expected);
    }
    cxpr_registry_free(registry);
    cxpr_context_free(context);
    return NULL;
}

static void test_independent_contexts_share_immutable_history(void) {
    static const double values[] = {10.0, 20.0, 30.0, 40.0};
    history_thread_case cases[] = {
        {values, 3, 60.0, false},
        {values, 2, 40.0, true},
    };
    pthread_t threads[2];
    assert(pthread_create(&threads[0], NULL, evaluate_shared_history, &cases[0]) == 0);
    assert(pthread_create(&threads[1], NULL, evaluate_shared_history, &cases[1]) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    assert(cases[0].cursor == 3 && cases[1].cursor == 2);
}

int main(void) {
    test_contiguous_dynamic_offsets_and_policies();
    test_strided_multiple_sources_and_isolated_cursors();
    test_invalid_dynamic_offsets();
    test_compound_expression_uses_immutable_shifted_context();
    test_dynamic_provider_rebinds_without_copying();
    test_malformed_and_overflowing_registration();
    test_independent_contexts_share_immutable_history();
    puts("cxpr generic history tests passed.");
    return 0;
}
