/**
 * @file eval.test.c
 * @brief Unit tests for the cxpr evaluator.
 *
 * Tests covered:
 * - Simple constant evaluation
 * - Variable lookup from context
 * - Parameter ($variable) substitution
 * - Function calls with registry
 * - Boolean expression evaluation
 * - Ternary expression evaluation
 * - Error handling (unknown identifier, division by zero)
 * - Field access evaluation
 * - Short-circuit logic (and/or)
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static double eval_ok(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    double result = cxpr_ast_eval(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static bool eval_bool_ok(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    assert(ast);
    bool result = cxpr_ast_eval_bool(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_error_code eval_error(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) { cxpr_parser_free(p); return err.code; }
    cxpr_ast_eval(ast, ctx, reg, &err);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return err.code;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_constant(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("42", ctx, reg), 42.0);
    ASSERT_DOUBLE_EQ(eval_ok("3.14", ctx, reg), 3.14);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_constant\n");
}

static void test_variable_lookup(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "rsi", 28.0);
    cxpr_context_set(ctx, "ema_fast", 101.0);
    ASSERT_DOUBLE_EQ(eval_ok("rsi", ctx, reg), 28.0);
    ASSERT_DOUBLE_EQ(eval_ok("ema_fast", ctx, reg), 101.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_variable_lookup\n");
}

static void test_param_substitution(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set_param(ctx, "oversold", 30.0);
    assert(eval_bool_ok("rsi < $oversold", ctx, reg) == true);
    cxpr_context_set(ctx, "rsi", 35.0);
    assert(eval_bool_ok("rsi < $oversold", ctx, reg) == false);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_param_substitution\n");
}

static void test_param_substitution_dotted(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "range_pct", 0.35);
    cxpr_context_set_param(ctx, "breakout.range_pct_min", 0.20);
    assert(eval_bool_ok("range_pct >= $breakout.range_pct_min", ctx, reg) == true);
    cxpr_context_set(ctx, "range_pct", 0.15);
    assert(eval_bool_ok("range_pct >= $breakout.range_pct_min", ctx, reg) == false);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_param_substitution_dotted\n");
}

static void test_arithmetic(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("2 + 3", ctx, reg), 5.0);
    ASSERT_DOUBLE_EQ(eval_ok("10 - 4", ctx, reg), 6.0);
    ASSERT_DOUBLE_EQ(eval_ok("3 * 7", ctx, reg), 21.0);
    ASSERT_DOUBLE_EQ(eval_ok("15 / 3", ctx, reg), 5.0);
    ASSERT_DOUBLE_EQ(eval_ok("17 % 5", ctx, reg), 2.0);
    ASSERT_DOUBLE_EQ(eval_ok("2 ^ 10", ctx, reg), 1024.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_arithmetic\n");
}

static void test_comparison(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("3 < 5", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("5 < 3", ctx, reg), 0.0);
    ASSERT_DOUBLE_EQ(eval_ok("3 == 3", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("3 != 4", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("5 >= 5", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("4 <= 5", ctx, reg), 1.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_comparison\n");
}

static void test_logical(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("true and true", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("true and false", ctx, reg), 0.0);
    ASSERT_DOUBLE_EQ(eval_ok("false or true", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("false or false", ctx, reg), 0.0);
    ASSERT_DOUBLE_EQ(eval_ok("not false", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("not true", ctx, reg), 0.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_logical\n");
}

static void test_function_call(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("abs(-5)", ctx, reg), 5.0);
    ASSERT_DOUBLE_EQ(eval_ok("min(3, 7)", ctx, reg), 3.0);
    ASSERT_DOUBLE_EQ(eval_ok("max(3, 7)", ctx, reg), 7.0);
    ASSERT_DOUBLE_EQ(eval_ok("min(8, 3, 7, 4)", ctx, reg), 3.0);
    ASSERT_DOUBLE_EQ(eval_ok("max(8, 3, 7, 4)", ctx, reg), 8.0);
    ASSERT_DOUBLE_EQ(eval_ok("sqrt(9)", ctx, reg), 3.0);
    ASSERT_DOUBLE_EQ(eval_ok("clamp(15, 0, 10)", ctx, reg), 10.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_function_call\n");
}

static void test_ternary(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "x", 5.0);
    ASSERT_DOUBLE_EQ(eval_ok("x > 0 ? x : 0", ctx, reg), 5.0);
    cxpr_context_set(ctx, "x", -3.0);
    ASSERT_DOUBLE_EQ(eval_ok("x > 0 ? x : 0", ctx, reg), 0.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_ternary\n");
}

static void test_field_access(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "macd.histogram", 0.5);
    cxpr_context_set(ctx, "adx.adx", 28.0);
    ASSERT_DOUBLE_EQ(eval_ok("macd.histogram", ctx, reg), 0.5);
    assert(eval_bool_ok("macd.histogram > 0 and adx.adx > 25", ctx, reg) == true);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_field_access\n");
}

static double test_macd_signal(const double* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 3);
    return args[0] - args[1] + args[2];
}

static void test_function_call_field_access(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_registry_add(reg, "macd.signal", test_macd_signal, 3, 3, NULL, NULL);
    ASSERT_DOUBLE_EQ(eval_ok("macd(12, 26, 9).signal", ctx, reg), -5.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_function_call_field_access\n");
}

static void test_unknown_identifier_error(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    assert(eval_error("unknown_var", ctx, reg) == CXPR_ERR_UNKNOWN_IDENTIFIER);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_unknown_identifier_error\n");
}

static void test_division_by_zero(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    assert(eval_error("1 / 0", ctx, reg) == CXPR_ERR_DIVISION_BY_ZERO);
    assert(eval_error("10 % 0", ctx, reg) == CXPR_ERR_DIVISION_BY_ZERO);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_division_by_zero\n");
}

static void test_unknown_function(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    assert(eval_error("nonexistent(1)", ctx, reg) == CXPR_ERR_UNKNOWN_FUNCTION);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_unknown_function\n");
}

static void test_wrong_arity(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    /* sqrt takes 1 arg, give it 2 */
    assert(eval_error("sqrt(1, 2)", ctx, reg) == CXPR_ERR_WRONG_ARITY);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_wrong_arity\n");
}

static double test_cross_above(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    return (args[0] > args[1]) ? 1.0 : 0.0;
}

static int g_userdata_free_count = 0;

static double test_user_data_passthrough(const double* args, size_t argc, void* ud) {
    (void)argc;
    const double scale = ud ? *(const double*)ud : 1.0;
    return args[0] * scale;
}

static void test_free_userdata(void* ud) {
    ++g_userdata_free_count;
    free(ud);
}

static void test_full_expression(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_registry_add(reg, "cross_above", test_cross_above, 2, 2, NULL, NULL);

    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set(ctx, "ema_fast", 101.0);
    cxpr_context_set(ctx, "ema_slow", 100.0);
    cxpr_context_set_param(ctx, "oversold", 30.0);

    assert(eval_bool_ok("rsi < $oversold and cross_above(ema_fast, ema_slow)", ctx, reg));

    /* Change to non-entry condition */
    cxpr_context_set(ctx, "rsi", 35.0);
    assert(!eval_bool_ok("rsi < $oversold and cross_above(ema_fast, ema_slow)", ctx, reg));

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_full_expression\n");
}

static void test_registry_userdata_cleanup(void) {
    g_userdata_free_count = 0;
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    double* ud1 = (double*)malloc(sizeof(double));
    assert(ud1 != NULL);
    *ud1 = 2.0;
    cxpr_registry_add(reg, "scale", test_user_data_passthrough, 1, 1, ud1, test_free_userdata);

    /* Overwrite should free previous userdata via callback. */
    double* ud2 = (double*)malloc(sizeof(double));
    assert(ud2 != NULL);
    *ud2 = 3.0;
    cxpr_registry_add(reg, "scale", test_user_data_passthrough, 1, 1, ud2, test_free_userdata);
    assert(g_userdata_free_count == 1);

    /* Registry teardown should free the latest userdata. */
    cxpr_registry_free(reg);
    assert(g_userdata_free_count == 2);
    printf("  ✓ test_registry_userdata_cleanup\n");
}

static void test_boolean_literals(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    ASSERT_DOUBLE_EQ(eval_ok("true", ctx, reg), 1.0);
    ASSERT_DOUBLE_EQ(eval_ok("false", ctx, reg), 0.0);
    ASSERT_DOUBLE_EQ(eval_ok("true and false", ctx, reg), 0.0);
    ASSERT_DOUBLE_EQ(eval_ok("true or false", ctx, reg), 1.0);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_boolean_literals\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Prefixed Fields Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_set_fields(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    /* Set multiple fields with prefix */
    const char* macd_fields[] = {"macdLine", "signalLine", "histogram"};
    double macd_values[] = {1.5, 1.2, 0.3};
    cxpr_context_set_fields(ctx, "macd", macd_fields, macd_values, 3);

    /* Evaluate field access expressions */
    ASSERT_DOUBLE_EQ(eval_ok("macd.macdLine", ctx, reg), 1.5);
    ASSERT_DOUBLE_EQ(eval_ok("macd.signalLine", ctx, reg), 1.2);
    ASSERT_DOUBLE_EQ(eval_ok("macd.histogram", ctx, reg), 0.3);

    /* Test expression with struct field */
    assert(eval_bool_ok("macd.histogram > 0", ctx, reg));
    assert(eval_bool_ok("macd.macdLine > macd.signalLine", ctx, reg));

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_set_fields\n");
}

static void test_multiple_prefixes(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    /* Set MACD fields */
    const char* macd_fields[] = {"histogram"};
    double macd_values[] = {0.5};
    cxpr_context_set_fields(ctx, "macd", macd_fields, macd_values, 1);

    /* Set Bollinger fields */
    const char* bb_fields[] = {"upper", "lower", "percentB"};
    double bb_values[] = {110.0, 90.0, 0.75};
    cxpr_context_set_fields(ctx, "bb", bb_fields, bb_values, 3);

    /* Cross-prefix expression */
    assert(eval_bool_ok("macd.histogram > 0 and bb.percentB > 0.5", ctx, reg));
    ASSERT_DOUBLE_EQ(eval_ok("bb.upper - bb.lower", ctx, reg), 20.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_multiple_prefixes\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running eval tests...\n");
    test_constant();
    test_variable_lookup();
    test_param_substitution();
    test_param_substitution_dotted();
    test_arithmetic();
    test_comparison();
    test_logical();
    test_function_call();
    test_ternary();
    test_field_access();
    test_function_call_field_access();
    test_boolean_literals();
    test_unknown_identifier_error();
    test_division_by_zero();
    test_unknown_function();
    test_wrong_arity();
    test_full_expression();
    test_registry_userdata_cleanup();

    /* Prefixed fields tests */
    test_set_fields();
    test_multiple_prefixes();

    printf("All eval tests passed!\n");
    return 0;
}
