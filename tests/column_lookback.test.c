#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    double close;
    double high;
} test_bar;

static cxpr_ast* parse_or_die(cxpr_parser* parser, const char* source) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, source, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n",
                source, err.message ? err.message : "(null)");
        assert(0);
    }
    return ast;
}

static double eval_number_or_die(cxpr_ast* ast, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    double out = NAN;
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    return out;
}

static void test_column_lookback_resolves_bound_columns(void) {
    static const test_bar bars[] = {
        {10.0, 11.0},
        {20.0, 22.0},
        {30.0, 33.0},
        {40.0, 44.0},
    };
    int64_t cursor = 3;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 4},
        {"high", &bars[0].high, sizeof(bars[0]), 4},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 2, &cursor));

    ast = parse_or_die(parser, "close[1] + high[2]");
    assert(eval_number_or_die(ast, ctx, reg) == 52.0);
    cxpr_ast_free(ast);

    cursor = 2;
    ast = parse_or_die(parser, "close[0] + high[1]");
    assert(eval_number_or_die(ast, ctx, reg) == 52.0);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_returns_nan_for_warmup_or_out_of_range(void) {
    static const test_bar bars[] = {
        {10.0, 11.0},
        {20.0, 22.0},
    };
    int64_t cursor = 0;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 2},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;
    double out;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 1, &cursor));

    ast = parse_or_die(parser, "close[1]");
    out = eval_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    cxpr_ast_free(ast);

    cursor = 4;
    ast = parse_or_die(parser, "close[0]");
    out = eval_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_leaves_unknown_or_dynamic_targets_unresolved(void) {
    static const test_bar bars[] = {
        {10.0, 11.0},
        {20.0, 22.0},
    };
    int64_t cursor = 1;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 2},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    double out = NAN;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 1, &cursor));

    ast = parse_or_die(parser, "open[0]");
    assert(!cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code != CXPR_OK);
    cxpr_ast_free(ast);

    err = (cxpr_error){0};
    out = NAN;
    cxpr_context_set_param(ctx, "n", 1.0);
    ast = parse_or_die(parser, "close[$n]");
    assert(!cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code != CXPR_OK);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_rejects_invalid_registration(void) {
    cxpr_registry* reg = cxpr_registry_new();
    assert(reg);

    assert(!cxpr_register_column_lookback(NULL, NULL, 0, NULL));
    assert(!cxpr_register_column_lookback(reg, NULL, 1, NULL));
    assert(cxpr_register_column_lookback(reg, NULL, 0, NULL));

    cxpr_registry_free(reg);
}

int main(void) {
    printf("Running column lookback tests...\n");
    test_column_lookback_resolves_bound_columns();
    test_column_lookback_returns_nan_for_warmup_or_out_of_range();
    test_column_lookback_leaves_unknown_or_dynamic_targets_unresolved();
    test_column_lookback_rejects_invalid_registration();
    printf("All column lookback tests passed!\n");
    return 0;
}
