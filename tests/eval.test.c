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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cxpr_test_internal.h"

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
    double result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
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
    bool result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Bool eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_error_code eval_error(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) { cxpr_parser_free(p); return err.code; }
    cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return err.code;
}

static void test_eval_bool_out_api(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "rsi < $oversold", &err);
    bool result = false;

    assert(ast);
    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set_param(ctx, "oversold", 30.0);
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &result, &err));
    assert(err.code == CXPR_OK);
    assert(result == true);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_bool_out_api\n");
}

static void test_eval_number_out_api_type_mismatch(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "1 < 2", &err);
    double result = 0.0;

    assert(ast);
    assert(!cxpr_eval_ast_number(ast, ctx, reg, &result, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_number_out_api_type_mismatch\n");
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
    assert(eval_bool_ok("3 < 5", ctx, reg) == true);
    assert(eval_bool_ok("5 < 3", ctx, reg) == false);
    assert(eval_bool_ok("3 == 3", ctx, reg) == true);
    assert(eval_bool_ok("3 != 4", ctx, reg) == true);
    assert(eval_bool_ok("5 >= 5", ctx, reg) == true);
    assert(eval_bool_ok("4 <= 5", ctx, reg) == true);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_comparison\n");
}

static void test_logical(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    assert(eval_bool_ok("true and true", ctx, reg) == true);
    assert(eval_bool_ok("true and false", ctx, reg) == false);
    assert(eval_bool_ok("false or true", ctx, reg) == true);
    assert(eval_bool_ok("false or false", ctx, reg) == false);
    assert(eval_bool_ok("not false", ctx, reg) == true);
    assert(eval_bool_ok("not true", ctx, reg) == false);
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

static void test_macd_signal_producer(const double* args, size_t argc,
                                      cxpr_value* out, size_t field_count,
                                      void* ud) {
    (void)ud;
    assert(argc == 3);
    assert(field_count == 1);
    out[0] = cxpr_fv_double(args[0] - args[1] + args[2]);
}

static void test_function_call_field_access(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    const char* fields[] = {"signal"};
    cxpr_register_builtins(reg);
    cxpr_registry_add_struct(reg, "macd", test_macd_signal_producer,
                                      3, 3, fields, 1, NULL, NULL);
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

static cxpr_value test_cross_above(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    return cxpr_fv_bool(args[0] > args[1]);
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

static cxpr_value test_is_valid_typed(const cxpr_value* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 2);
    assert(args[0].type == CXPR_VALUE_BOOL);
    assert(args[1].type == CXPR_VALUE_NUMBER);
    return cxpr_fv_bool(args[0].b && args[1].d >= 3.0);
}

static double test_macd_field_number(const cxpr_struct_value* s, const char* field) {
    assert(s != NULL);
    for (size_t i = 0; i < s->field_count; ++i) {
        if (strcmp(s->field_names[i], field) == 0) {
            assert(s->field_values[i].type == CXPR_VALUE_NUMBER);
            return s->field_values[i].d;
        }
    }
    assert(!"macd field missing");
    return 0.0;
}

static cxpr_value test_macd_signal_ok(const cxpr_value* args, size_t argc, void* ud) {
    double min_hist = ud ? *(const double*)ud : 0.0;
    double line;
    double signal;
    double hist;
    assert(argc == 2);
    assert(args[0].type == CXPR_VALUE_STRUCT);
    assert(args[1].type == CXPR_VALUE_NUMBER);
    line = test_macd_field_number(args[0].s, "line");
    signal = test_macd_field_number(args[0].s, "signal");
    hist = test_macd_field_number(args[0].s, "histogram");
    return cxpr_fv_bool(line > signal && hist > min_hist && hist > args[1].d);
}

static void test_macd_producer(const double* args, size_t argc,
                               cxpr_value* out, size_t field_count,
                               void* userdata) {
    (void)argc;
    (void)userdata;
    assert(field_count == 3);
    out[0] = cxpr_fv_double(args[0] + 1.0);
    out[1] = cxpr_fv_double(args[0] - 0.5);
    out[2] = cxpr_fv_double(args[0] * 0.25);
}

static void test_typed_function_bool_argument(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;
    const char* field_names[] = {"active", "score"};
    const cxpr_value_type sig[] = {CXPR_VALUE_BOOL, CXPR_VALUE_NUMBER};
    cxpr_value sensor_values[] = {cxpr_fv_bool(true), cxpr_fv_double(3.5)};
    cxpr_struct_value* sensor = cxpr_struct_value_new(field_names, sensor_values, 2);
    assert(sensor != NULL);

    cxpr_register_builtins(reg);
    cxpr_registry_add_typed(reg, "is_valid", test_is_valid_typed, 2, 2,
                            sig, CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_context_set_struct(ctx, "sensor", sensor);

    ast = cxpr_parse(p, "is_valid(sensor.active, sensor.score)", &err);
    assert(ast != NULL);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(err.code == CXPR_OK);

    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == true);
    assert(err.code == CXPR_OK);

    sensor_values[0] = cxpr_fv_bool(false);
    sensor = cxpr_struct_value_new(field_names, sensor_values, 2);
    assert(sensor != NULL);
    cxpr_context_set_struct(ctx, "sensor", sensor);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == false);
    assert(err.code == CXPR_OK);

    assert(eval_error("is_valid(1.0, 3.5)", ctx, reg) == CXPR_ERR_TYPE_MISMATCH);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_typed_function_bool_argument\n");
}

static void test_full_expression(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_registry_add_value(reg, "cross_above", test_cross_above, 2, 2, NULL, NULL);

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

static void test_typed_function_struct_argument(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;
    const char* field_names[] = {"line", "signal", "histogram"};
    const cxpr_value_type sig[] = {CXPR_VALUE_STRUCT, CXPR_VALUE_NUMBER};
    cxpr_value macd_values[] = {
        cxpr_fv_double(2.0),
        cxpr_fv_double(1.0),
        cxpr_fv_double(0.6),
    };
    cxpr_struct_value* macd_ctx = cxpr_struct_value_new(field_names, macd_values, 3);
    const char* producer_fields[] = {"line", "signal", "histogram"};
    double* min_hist = (double*)malloc(sizeof(double));

    assert(macd_ctx != NULL);
    assert(min_hist != NULL);
    *min_hist = 0.1;
    cxpr_register_builtins(reg);
    cxpr_registry_add_typed(reg, "macd_signal_ok", test_macd_signal_ok, 2, 2,
                            sig, CXPR_VALUE_BOOL, min_hist, test_free_userdata);
    cxpr_registry_add_struct(reg, "macd", test_macd_producer,
                             1, 1, producer_fields, 3, NULL, NULL);
    cxpr_context_set_struct(ctx, "macd_ctx", macd_ctx);

    ast = cxpr_parse(p,
        "macd_signal_ok(macd_ctx, 0.5) and macd_signal_ok(macd(2.5), 0.4)",
        &err);
    assert(ast != NULL);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(err.code == CXPR_OK);

    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).type == CXPR_VALUE_BOOL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == true);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).type == CXPR_VALUE_BOOL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == true);

    assert(eval_error("macd_signal_ok(1.0, 0.5)", ctx, reg) == CXPR_ERR_TYPE_MISMATCH);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_typed_function_struct_argument\n");
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

static double test_scale2(const double* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 1);
    return args[0] * 2.0;
}

static double test_scale3(const double* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 1);
    return args[0] * 3.0;
}

static void test_ast_function_cache_invalidates_on_registry_change(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    cxpr_registry_add(reg, "scale", test_scale2, 1, 1, NULL, NULL);
    ast = cxpr_parse(p, "scale(5)", &err);
    assert(ast);

    ASSERT_DOUBLE_EQ(cxpr_test_eval_ast_number(ast, ctx, reg, &err), 10.0);
    assert(err.code == CXPR_OK);

    cxpr_registry_add(reg, "scale", test_scale3, 1, 1, NULL, NULL);
    ASSERT_DOUBLE_EQ(cxpr_test_eval_ast_number(ast, ctx, reg, &err), 15.0);
    assert(err.code == CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_function_cache_invalidates_on_registry_change\n");
}

static void test_typed_if(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "if(true, false, true)", &err);
    cxpr_program* prog;
    assert(ast != NULL);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).type == CXPR_VALUE_BOOL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_ast(ast, ctx, reg, &err).b == false);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).type == CXPR_VALUE_BOOL);
    assert(err.code == CXPR_OK);
    assert(cxpr_test_eval_program(prog, ctx, reg, &err).b == false);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_typed_if\n");
}

static void test_boolean_literals(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    assert(eval_bool_ok("true", ctx, reg) == true);
    assert(eval_bool_ok("false", ctx, reg) == false);
    assert(eval_bool_ok("true and false", ctx, reg) == false);
    assert(eval_bool_ok("true or false", ctx, reg) == true);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_boolean_literals\n");
}

static void test_defined_function_scalar_ir_path(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_func_entry* sq_entry;
    cxpr_func_entry* hyp_entry;
    cxpr_register_builtins(reg);

    assert(cxpr_registry_define_fn(reg, "sq(x) => x * x").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "hyp(a, b) => sqrt(sq(a) + sq(b))").code == CXPR_OK);

    sq_entry = cxpr_registry_find(reg, "sq");
    hyp_entry = cxpr_registry_find(reg, "hyp");
    assert(sq_entry != NULL);
    assert(hyp_entry != NULL);
    assert(sq_entry->defined_program == NULL);
    assert(hyp_entry->defined_program == NULL);

    ASSERT_DOUBLE_EQ(eval_ok("hyp(3, 4) + sq(5)", ctx, reg), 30.0);

    assert(sq_entry->defined_program != NULL);
    assert(hyp_entry->defined_program != NULL);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_defined_function_scalar_ir_path\n");
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
    test_eval_bool_out_api();
    test_eval_number_out_api_type_mismatch();
    test_arithmetic();
    test_comparison();
    test_logical();
    test_function_call();
    test_ternary();
    test_field_access();
    test_function_call_field_access();
    test_typed_if();
    test_boolean_literals();
    test_defined_function_scalar_ir_path();
    test_unknown_identifier_error();
    test_division_by_zero();
    test_unknown_function();
    test_wrong_arity();
    test_full_expression();
    test_typed_function_bool_argument();
    test_typed_function_struct_argument();
    test_registry_userdata_cleanup();
    test_ast_function_cache_invalidates_on_registry_change();

    /* Prefixed fields tests */
    test_set_fields();
    test_multiple_prefixes();

    printf("All eval tests passed!\n");
    return 0;
}
