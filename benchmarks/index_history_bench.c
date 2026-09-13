#include <cxpr/cxpr.h>

#include "index_array_generated.h"
#include "index_history_generated.h"
#include "producer_history_generated.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ITERATIONS 200000u

static volatile double sink;

static long long now_ns(void) {
    struct timespec time;
    timespec_get(&time, TIME_UTC);
    return (long long)time.tv_sec * 1000000000LL + time.tv_nsec;
}

static void sample(const double* args, size_t argc, cxpr_value* out,
                   size_t field_count, void* userdata) {
    (void)userdata;
    assert(argc == 1u && field_count == 1u);
    out[0] = cxpr_num(args[0] * 2.0);
}

static cxpr_expr_ast* parse(cxpr_expr_parser* parser, const char* source) {
    cxpr_error error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &error);
    assert(ast && error.code == CXPR_OK);
    return ast;
}

static double time_ast(const cxpr_expr_ast* ast, cxpr_context* context,
                       cxpr_registry* registry) {
    cxpr_error error = {0};
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) {
        cxpr_value value = cxpr_null();
        if (!cxpr_eval_ast(ast, context, registry, &value, &error) ||
            value.type != CXPR_VALUE_NUMBER) abort();
        sink += value.d;
        cxpr_value_free(&value);
    }
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_ir(const cxpr_expr_compiled* program, cxpr_context* context,
                      cxpr_registry* registry) {
    cxpr_error error = {0};
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) {
        cxpr_value value = cxpr_null();
        if (!cxpr_expr_compiled_eval(program, context, registry, &value, &error) ||
            value.type != CXPR_VALUE_NUMBER) abort();
        sink += value.d;
        cxpr_value_free(&value);
    }
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_direct_array(void) {
    static const double values[] = {10.0, 20.0, 30.0};
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) sink += values[1];
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_generated_array(void) {
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) sink += generated_number_index(1.0);
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_direct_history(const double* values, int64_t cursor) {
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) sink += values[cursor - 1];
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_generated_history(void) {
    cxpr_bench_generated_close1_state state = {0};
    const double params[1] = {0.0};
    double input[1] = {10.0};
    double output[1] = {0.0};
    cxpr_bench_generated_close1(&state, input, params, output);
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) {
        input[0] = 11.0 + (double)(i & 1u);
        cxpr_bench_generated_close1(&state, input, params, output);
        sink += output[0];
    }
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

static double time_generated_producer_history(void) {
    cxpr_bench_generated_producer_history_state state = {0};
    const double params[1] = {0.0};
    double input[1] = {10.0};
    double output[1] = {0.0};
    cxpr_bench_generated_producer_history(&state, input, params, output);
    long long start = now_ns();
    for (size_t i = 0u; i < ITERATIONS; ++i) {
        input[0] = 11.0 + (double)(i & 1u);
        cxpr_bench_generated_producer_history(&state, input, params, output);
        sink += output[0];
    }
    return (double)(now_ns() - start) / (double)ITERATIONS;
}

int main(void) {
    static const double close_values[] = {10.0, 11.0, 12.0, 13.0};
    static const char* fields[] = {"signal"};
    int64_t cursor = 3;
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_expr_ast* array_ast;
    cxpr_expr_ast* history_ast;
    cxpr_expr_ast* producer_ast;
    cxpr_expr_compiled* array_ir;
    cxpr_expr_compiled* history_ir;
    cxpr_expr_compiled* producer_ir;

    if (!parser || !registry || !context) abort();
    cxpr_register_defaults(registry);
    cxpr_registry_add_struct(registry, "sample", sample, 1u, 1u,
                             fields, 1u, NULL, NULL);
    if (!cxpr_register_history_contiguous_numbers(
            registry, "close", close_values, 4u, &cursor,
            CXPR_HISTORY_BOUNDS_ERROR)) abort();
    cxpr_context_set_param(context, "i", 1.0);
    array_ast = parse(parser, "[10, 20, 30][$i]");
    history_ast = parse(parser, "close[1]");
    producer_ast = parse(parser, "sample(close).signal[1]");
    array_ir = cxpr_expr_compile(array_ast, registry, &error);
    history_ir = cxpr_expr_compile(history_ast, registry, &error);
    producer_ir = cxpr_expr_compile(producer_ast, registry, &error);
    if (!array_ir || !history_ir || !producer_ir) abort();

    puts("Plan 71 index/history benchmark (200000 iterations, ns/eval)");
    printf("%-24s %9s %9s %12s %9s\n", "case", "baseline", "AST", "IR", "generated");
    printf("%-24s %9.2f %9.2f %12.2f %9.2f\n", "array index",
           time_direct_array(), time_ast(array_ast, context, registry),
           time_ir(array_ir, context, registry), time_generated_array());
    printf("%-24s %9.2f %9.2f %12.2f %9.2f\n", "close[1]",
           time_direct_history(close_values, cursor),
           time_ast(history_ast, context, registry),
           time_ir(history_ir, context, registry), time_generated_history());
    printf("%-24s %9s %9.2f %12.2f %9.2f\n", "producer-field[1]", "unsupported",
           time_ast(producer_ast, context, registry),
           time_ir(producer_ir, context, registry), time_generated_producer_history());
    printf("sink=%.0f\n", sink);
    if (fabs(sink - 39699994.0) > 0.5) abort();

    cxpr_expr_compiled_free(producer_ir);
    cxpr_expr_compiled_free(history_ir);
    cxpr_expr_compiled_free(array_ir);
    cxpr_expr_ast_free(producer_ast);
    cxpr_expr_ast_free(history_ast);
    cxpr_expr_ast_free(array_ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return 0;
}
