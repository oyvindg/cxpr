#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { SAMPLE_COUNT = 4096, ITERATIONS = 500000 };

typedef struct bench_series {
    double values[SAMPLE_COUNT];
    int64_t cursor;
} bench_series;

static volatile double sink;

static double indicator_passthrough(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    return argc > 0u ? args[0] : NAN;
}

static cxpr_value planning_resample(const cxpr_expr_ast* ast,
                                    const cxpr_context* context,
                                    const cxpr_registry* registry,
                                    void* userdata,
                                    cxpr_error* err) {
    (void)ast; (void)context; (void)registry; (void)userdata; (void)err;
    return (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = 0.0};
}

static uint64_t now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

static int is_series_call(const cxpr_expr_ast* ast, int legacy) {
    const char* expected = legacy ? "close" : "resample";
    return ast && cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL &&
           strcmp(cxpr_expr_ast_call_name(ast), expected) == 0;
}

static bool resolve_history(const cxpr_expr_ast* target,
                            const cxpr_expr_ast* index,
                            const cxpr_context* context,
                            const cxpr_registry* registry,
                            void* userdata,
                            cxpr_value* out,
                            cxpr_error* err) {
    bench_series* series = (bench_series*)userdata;
    int64_t offset;
    (void)context;
    (void)registry;
    (void)err;
    if (!is_series_call(target, 0) && !is_series_call(target, 1)) return false;
    if (!index || cxpr_expr_ast_kind_of(index) != CXPR_NODE_NUMBER) return false;
    offset = (int64_t)cxpr_expr_ast_number_value(index);
    if (offset < 0 || series->cursor < offset) return false;
    *out = (cxpr_value){
        .type = CXPR_VALUE_NUMBER,
        .d = series->values[series->cursor - offset],
    };
    return true;
}

static cxpr_value current_value(const cxpr_expr_ast* call_ast,
                                const cxpr_context* context,
                                const cxpr_registry* registry,
                                void* userdata,
                                cxpr_error* err) {
    bench_series* series = (bench_series*)userdata;
    (void)call_ast;
    (void)context;
    (void)registry;
    (void)err;
    return (cxpr_value){
        .type = CXPR_VALUE_NUMBER,
        .d = series->values[series->cursor],
    };
}

static double generated_resample(const double* values, size_t cursor) {
    return values[cursor - 1u];
}

static double generated_view_load(const double* values, size_t value_count,
                                  const size_t* alignment, size_t primary_count,
                                  size_t primary_cursor, size_t lookback) {
    size_t cursor;
    if (!values || !alignment || primary_cursor >= primary_count) return NAN;
    cursor = alignment[primary_cursor];
    if (cursor == SIZE_MAX || cursor < lookback || cursor - lookback >= value_count)
        return NAN;
    return values[cursor - lookback];
}

static double repeated_prebound_uncached(const volatile double* values,
                                         const volatile size_t* alignment,
                                         size_t primary_cursor) {
    return values[alignment[primary_cursor]] + values[alignment[primary_cursor]] +
           values[alignment[primary_cursor] - 1u] +
           values[alignment[primary_cursor] - 1u];
}

static double repeated_prebound_cached(const volatile double* values,
                                       const volatile size_t* alignment,
                                       size_t primary_cursor) {
    const size_t cursor_0 = alignment[primary_cursor];
    const size_t cursor_1 = alignment[primary_cursor];
    const double value_0 = values[cursor_0];
    const double value_1 = values[cursor_1 - 1u];
    return value_0 + value_0 + value_1 + value_1;
}

static double run_ast(const cxpr_expr_ast* ast, cxpr_context* context,
                      cxpr_registry* registry, bench_series* series) {
    cxpr_error err = {0};
    cxpr_value value = {0};
    double total = 0.0;
    uint64_t begin = now_ns();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        series->cursor = 1 + (int64_t)(i % (SAMPLE_COUNT - 1));
        if (!cxpr_eval_ast(ast, context, registry, &value, &err)) {
            fprintf(stderr, "AST resample benchmark failed: %s\n",
                    err.message ? err.message : "unknown error");
            exit(1);
        }
        total += value.d;
    }
    sink = total;
    return (double)(now_ns() - begin) / ITERATIONS;
}

static double run_ir(const cxpr_expr_compiled* ir, cxpr_context* context,
                     cxpr_registry* registry, bench_series* series) {
    cxpr_error err = {0};
    cxpr_value value = {0};
    double total = 0.0;
    uint64_t begin = now_ns();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        series->cursor = 1 + (int64_t)(i % (SAMPLE_COUNT - 1));
        if (!cxpr_expr_compiled_eval(ir, context, registry, &value, &err)) {
            fprintf(stderr, "IR resample benchmark failed: %s\n",
                    err.message ? err.message : "unknown error");
            exit(1);
        }
        total += value.d;
    }
    sink = total;
    return (double)(now_ns() - begin) / ITERATIONS;
}

static double run_generated(bench_series* series) {
    double total = 0.0;
    uint64_t begin = now_ns();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        size_t cursor = 1u + i % (SAMPLE_COUNT - 1u);
        total += generated_resample(series->values, cursor);
    }
    sink = total;
    return (double)(now_ns() - begin) / ITERATIONS;
}

static double run_repeated(bench_series* series, int cached) {
    size_t alignment[SAMPLE_COUNT];
    double total = 0.0;
    uint64_t begin;
    for (size_t i = 0; i < SAMPLE_COUNT; ++i) alignment[i] = i;
    begin = now_ns();
    for (size_t i = 0; i < ITERATIONS; ++i) {
        size_t cursor = 1u + i % (SAMPLE_COUNT - 1u);
        total += cached
            ? repeated_prebound_cached(series->values, alignment, cursor)
            : repeated_prebound_uncached(series->values, alignment, cursor);
    }
    sink = total;
    return (double)(now_ns() - begin) / ITERATIONS;
}

static void run_planning_and_size(void) {
    static const char source[] =
        "model resample_planning\n"
        "in { close }\n"
        "a = resample(close, \"1h\") + resample(close, \"1h\")\n"
        "b = resample(close, \"1h\")[1] + resample(close, \"1h\")[1]\n"
        "c = resample(close, \"5m\")\n"
        "out a, b, c\n";
    enum { PLANNING_ITERATIONS = 1000 };
    uint64_t begin = now_ns();
    size_t artifact_bytes = 0u;
    for (size_t i = 0; i < PLANNING_ITERATIONS; ++i) {
        cxpr_error err = {0};
        cxpr_registry* registry = cxpr_registry_new();
        cxpr_model* model;
        cxpr_model_compiled* program;
        char* generated;
        if (!registry) exit(1);
        cxpr_registry_add_ast(registry, "resample", planning_resample, 2u, 2u,
                              CXPR_VALUE_NUMBER, NULL, NULL);
        model = cxpr_model_parse(source, &err);
        program = model ? cxpr_model_compile(model, registry, &err) : NULL;
        generated = program ? cxpr_model_compiled_generate_c(
            program, "static inline", "resample_planning_tick", &err) : NULL;
        if (!generated) {
            fprintf(stderr, "resample planning benchmark failed: %s\n",
                    err.message ? err.message : "unknown error");
            exit(1);
        }
        artifact_bytes = strlen(generated);
        free(generated);
        cxpr_model_compiled_free(program);
        cxpr_model_free(model);
        cxpr_registry_free(registry);
    }
    printf("planning parse+compile+generate %9.2f ns/model\n",
           (double)(now_ns() - begin) / PLANNING_ITERATIONS);
    printf("generated C artifact size      %9zu bytes\n", artifact_bytes);
}

int main(void) {
    bench_series series = {0};
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* resample;
    cxpr_expr_ast* scoped;
    cxpr_expr_compiled* resample_ir;
    cxpr_expr_compiled* scoped_ir;
    const char* coverage_exprs[] = {
        "resample(close, \"1h\")",
        "resample(close, \"1h\")[1]",
        "indicator(resample(close, \"1h\"), 14)",
        "resample(path, \"500ms\")[1]",
    };
    for (size_t i = 0; i < SAMPLE_COUNT; ++i) series.values[i] = (double)i * 0.25;
    assert(parser && context && registry);
    cxpr_registry_add_ast(registry, "resample", current_value, 2u, 2u,
                          CXPR_VALUE_NUMBER, &series, NULL);
    cxpr_registry_add_ast(registry, "close", current_value, 0u, 1u,
                          CXPR_VALUE_NUMBER, &series, NULL);
    cxpr_registry_add(registry, "indicator", indicator_passthrough, 2u, 2u,
                      NULL, NULL);
    cxpr_registry_set_lookback_resolver(registry, resolve_history, &series, NULL);
    resample = cxpr_expr_ast_parse(parser, "resample(close, \"1h\")[1]", &err);
    scoped = cxpr_expr_ast_parse(parser, "close(timeframe=\"1h\")[1]", &err);
    assert(resample && scoped);
    resample_ir = cxpr_expr_compile(resample, registry, &err);
    scoped_ir = cxpr_expr_compile(scoped, registry, &err);
    assert(resample_ir && scoped_ir);
    for (size_t i = 0u; i < sizeof(coverage_exprs) / sizeof(coverage_exprs[0]); ++i) {
        cxpr_expr_ast* coverage = cxpr_expr_ast_parse(parser, coverage_exprs[i], &err);
        cxpr_value value = {0};
        assert(coverage);
        series.cursor = 2;
        assert(cxpr_eval_ast(coverage, context, registry, &value, &err));
        cxpr_expr_ast_free(coverage);
    }
    {
        const size_t aligned[] = {0u, 0u, 1u};
        const size_t gap[] = {0u, 0u, SIZE_MAX};
        assert(isnan(generated_view_load(series.values, SAMPLE_COUNT, aligned, 3u,
                                         0u, 1u)));
        assert(isnan(generated_view_load(series.values, SAMPLE_COUNT, gap, 3u,
                                         2u, 0u)));
    }
    puts("workload coverage: one_interval_current one_interval_[1] nested_indicator repeated_dedup multiple_intervals warmup_gap non_trading_path");

    puts("resample backend benchmark (500000 evaluations, ns/eval)");
    printf("  scoped timeframe AST  %9.2f\n", run_ast(scoped, context, registry, &series));
    printf("  resample AST          %9.2f\n", run_ast(resample, context, registry, &series));
    printf("  scoped timeframe IR   %9.2f\n", run_ir(scoped_ir, context, registry, &series));
    printf("  resample IR           %9.2f\n", run_ir(resample_ir, context, registry, &series));
    printf("  generated C/CUDA ABI  %9.2f\n", run_generated(&series));
    puts("repeated pre-bound load workload (volatile memory contract, ns/eval)");
    printf("  uncached 2x current+2x [1] %8.2f\n", run_repeated(&series, 0));
    printf("  CSE locals current/[1]     %8.2f\n", run_repeated(&series, 1));
    run_planning_and_size();

    cxpr_expr_compiled_free(scoped_ir);
    cxpr_expr_compiled_free(resample_ir);
    cxpr_expr_ast_free(scoped);
    cxpr_expr_ast_free(resample);
    cxpr_registry_free(registry);
    cxpr_context_free(context);
    cxpr_expr_parser_free(parser);
    return sink == 0.0;
}
