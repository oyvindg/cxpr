/**
 * @file int64_ir_bench.c
 * @brief Repeated public-API benchmark for closed int64 IR execution.
 */

#include <cxpr/cxpr.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

int main(void) {
    const size_t iterations = 10000000u;
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "9007199254740993i64", &err);
    cxpr_expr_compiled* program = ast ? cxpr_expr_compile(ast, NULL, &err) : NULL;
    int64_t total = 0;
    uint64_t start;
    uint64_t elapsed;

    if (!parser || !ctx || !program) {
        fprintf(stderr, "int64 benchmark setup failed: %s\n",
                err.message ? err.message : "allocation failure");
        return EXIT_FAILURE;
    }
    start = now_ns();
    for (size_t i = 0u; i < iterations; ++i) {
        cxpr_value value = {0};
        if (!cxpr_expr_compiled_eval(program, ctx, NULL, &value, &err) ||
            value.type != CXPR_VALUE_INT64) {
            fprintf(stderr, "int64 benchmark evaluation failed: %s\n",
                    err.message ? err.message : "wrong result kind");
            return EXIT_FAILURE;
        }
        total += value.i64 & INT64_C(1);
    }
    elapsed = now_ns() - start;
    printf("closed_int64 iterations=%zu ns/eval=%.2f throughput/s=%.0f sink=%" PRId64 "\n",
           iterations, (double)elapsed / (double)iterations,
           (double)iterations * 1e9 / (double)elapsed, total);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_expr_parser_free(parser);
    return EXIT_SUCCESS;
}

