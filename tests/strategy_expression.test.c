/**
 * @file strategy_expression.test.c
 * @brief Complex strategy expression tests with MACD-like scenarios.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cxpr_test_internal.h"

static cxpr_value test_cross_above_strategy(const double* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 2);
    return cxpr_fv_bool(args[0] > args[1]);
}

static void test_macd_strategy_producer(const double* args, size_t argc,
                                        cxpr_value* out, size_t field_count,
                                        void* userdata) {
    const double fast = args[0];
    const double slow = args[1];
    const double period = args[2];
    const double line = fast - (slow * 0.5);
    const double signal = line - (period * 0.1);
    const double histogram = line - signal;
    (void)userdata;
    assert(argc == 3);
    assert(field_count == 3);
    out[0] = cxpr_fv_double(line);
    out[1] = cxpr_fv_double(signal);
    out[2] = cxpr_fv_double(histogram);
}

static void test_parse_complex_macd_strategy_pipe(void) {
    const char* producer_fields[] = {"line", "signal", "histogram"};
    const char* producer_params[] = {"fast", "slow", "period"};
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(
        p,
        "(macd(fast=12, slow=26, period=9).histogram |> abs |> clamp(0, 1)) > $hist_min and "
        "cross_above(ema_fast, ema_slow) and (rsi < $oversold ? adx_val > 20 : adx_val > 30)",
        &err);

    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    cxpr_register_defaults(reg);
    cxpr_registry_add_struct(reg, "macd", test_macd_strategy_producer,
                             3, 3, producer_fields, 3, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", producer_params, 3));
    cxpr_registry_add_value(reg, "cross_above", test_cross_above_strategy, 2, 2, NULL, NULL);
    assert(cxpr_analyze(ast, reg, &info, &err));
    assert(err.code == CXPR_OK);
    assert(info.result_type == CXPR_EXPR_BOOL);
    assert(info.uses_functions == true);
    assert(info.uses_parameters == true);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_complex_macd_strategy_pipe\n");
}

static void test_complex_macd_strategy_pipe_matches_nested(void) {
    const char* producer_fields[] = {"line", "signal", "histogram"};
    const char* producer_params[] = {"fast", "slow", "period"};
    const char* expr_pipe =
        "(macd(fast=12, slow=26, period=9).histogram |> abs |> clamp(0, 1)) > $hist_min and "
        "cross_above(ema_fast, ema_slow) and (rsi < $oversold ? adx_val > 20 : adx_val > 30)";
    const char* expr_nested =
        "clamp(abs(macd(fast=12, slow=26, period=9).histogram), 0, 1) > $hist_min and "
        "cross_above(ema_fast, ema_slow) and (rsi < $oversold ? adx_val > 20 : adx_val > 30)";

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast_pipe;
    cxpr_ast* ast_nested;
    cxpr_program* prog_pipe;
    cxpr_program* prog_nested;
    cxpr_value ast_pipe_v;
    cxpr_value ast_nested_v;
    cxpr_value prog_pipe_v;
    cxpr_value prog_nested_v;

    cxpr_register_defaults(reg);
    cxpr_registry_add_struct(reg, "macd", test_macd_strategy_producer,
                             3, 3, producer_fields, 3, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", producer_params, 3));
    cxpr_registry_add_value(reg, "cross_above", test_cross_above_strategy, 2, 2, NULL, NULL);

    ast_pipe = cxpr_parse(p, expr_pipe, &err);
    assert(ast_pipe != NULL);
    assert(err.code == CXPR_OK);
    ast_nested = cxpr_parse(p, expr_nested, &err);
    assert(ast_nested != NULL);
    assert(err.code == CXPR_OK);

    prog_pipe = cxpr_compile(ast_pipe, reg, &err);
    assert(prog_pipe != NULL);
    assert(err.code == CXPR_OK);
    prog_nested = cxpr_compile(ast_nested, reg, &err);
    assert(prog_nested != NULL);
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "ema_fast", 105.0);
    cxpr_context_set(ctx, "ema_slow", 100.0);
    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set(ctx, "adx_val", 22.0);
    cxpr_context_set_param(ctx, "hist_min", 0.6);
    cxpr_context_set_param(ctx, "oversold", 30.0);

    ast_pipe_v = cxpr_test_eval_ast(ast_pipe, ctx, reg, &err);
    assert(err.code == CXPR_OK && ast_pipe_v.type == CXPR_VALUE_BOOL && ast_pipe_v.b == true);
    ast_nested_v = cxpr_test_eval_ast(ast_nested, ctx, reg, &err);
    assert(err.code == CXPR_OK && ast_nested_v.type == CXPR_VALUE_BOOL && ast_nested_v.b == true);
    prog_pipe_v = cxpr_test_eval_program(prog_pipe, ctx, reg, &err);
    assert(err.code == CXPR_OK && prog_pipe_v.type == CXPR_VALUE_BOOL && prog_pipe_v.b == true);
    prog_nested_v = cxpr_test_eval_program(prog_nested, ctx, reg, &err);
    assert(err.code == CXPR_OK && prog_nested_v.type == CXPR_VALUE_BOOL && prog_nested_v.b == true);

    cxpr_context_set(ctx, "rsi", 40.0);
    cxpr_context_set(ctx, "adx_val", 22.0);
    assert(cxpr_test_eval_ast(ast_pipe, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog_pipe, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "adx_val", 35.0);
    assert(cxpr_test_eval_ast(ast_pipe, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog_pipe, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    cxpr_program_free(prog_pipe);
    cxpr_program_free(prog_nested);
    cxpr_ast_free(ast_pipe);
    cxpr_ast_free(ast_nested);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_complex_macd_strategy_pipe_matches_nested\n");
}

static void test_strategy_pipe_rhs_must_be_callable(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(
        p,
        "macd(12, 26, 9).histogram |> abs + 1 > 0 and cross_above(ema_fast, ema_slow)",
        &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strstr(err.message, "Expected callable after '|>'") != NULL);
    cxpr_parser_free(p);
    printf("  ✓ test_strategy_pipe_rhs_must_be_callable\n");
}

static void test_pipe_gt_edgecases(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "(x |> abs) > y and (y |> abs) >= 0", &err);
    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "x |> abs > y", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strstr(err.message, "Expected callable after '|>'") != NULL);

    cxpr_parser_free(p);
    printf("  ✓ test_pipe_gt_edgecases\n");
}

static void test_pipe_rhs_error_variants(void) {
    const char* bad_exprs[] = {
        "x |> (abs + 1)",
        "x |> 1",
        "x |> true",
        "x |> abs and y",
        "x|>abs>=y",
    };
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};

    for (size_t i = 0; i < sizeof(bad_exprs) / sizeof(bad_exprs[0]); ++i) {
        cxpr_ast* ast = cxpr_parse(p, bad_exprs[i], &err);
        assert(ast == NULL);
        assert(err.code == CXPR_ERR_SYNTAX);
        assert(err.message != NULL);
        assert(strstr(err.message, "Expected callable after '|>'") != NULL);
    }

    cxpr_parser_free(p);
    printf("  ✓ test_pipe_rhs_error_variants\n");
}

static void test_pipe_stage_allows_ternary_argument(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;

    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "x", -4.0);
    cxpr_context_set(ctx, "flag", 1.0);
    cxpr_context_set(ctx, "y", 10.0);

    ast = cxpr_parse(p, "(x |> clamp(0, flag > 0 ? 3 : 8)) < y", &err);
    assert(ast != NULL);
    assert(err.code == CXPR_OK);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "flag", 0.0);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_pipe_stage_allows_ternary_argument\n");
}

static void test_pipe_in_logical_composition(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "hist", -0.8);
    cxpr_context_set(ctx, "adx", 27.0);

    ast = cxpr_parse(p, "((hist |> abs |> clamp(0, 1)) > 0.5 and adx > 25) or ((hist |> abs) < 0.2)", &err);
    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "hist", 0.1);
    cxpr_context_set(ctx, "adx", 20.0);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "hist", 0.4);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_pipe_in_logical_composition\n");
}

static void test_named_in_square_brackets(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "macd.signal", 15.0);

    ast = cxpr_parse(p, "macd.signal in [min=10, max=20]", &err);
    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "macd.signal not in [max=20, min=10]", &err);
    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "macd.signal not in (min=10, max=20)", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strstr(err.message, "Expected '['") != NULL);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_named_in_square_brackets\n");
}

int main(void) {
    printf("Running strategy expression tests...\n");
    test_parse_complex_macd_strategy_pipe();
    test_complex_macd_strategy_pipe_matches_nested();
    test_strategy_pipe_rhs_must_be_callable();
    test_pipe_gt_edgecases();
    test_pipe_rhs_error_variants();
    test_pipe_stage_allows_ternary_argument();
    test_pipe_in_logical_composition();
    test_named_in_square_brackets();
    printf("All strategy expression tests passed!\n");
    return 0;
}
