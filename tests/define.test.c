/**
 * @file define.test.c
 * @brief Tests for cxpr_registry_define_fn — expression-based function definitions.
 *
 * Tests covered:
 * - Scalar params:  sum(a, b) => a + b
 * - Struct params:  distance3(goal, pose) => sqrt(...)
 * - Mixed fields:   dot2(u, v) => u.x*v.x + u.y*v.y
 * - $params in body: clamp_val(p) => clamp(p.value, $lo, $hi)
 * - Defined fn calling another defined fn
 * - Overwrite with a new definition
 * - Error: parse error in body
 * - Error: wrong argument count at call time
 * - Error: struct arg not an identifier
 * - Error: missing field in context
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "cxpr_test_internal.h"

#define EPSILON 1e-9
#define ASSERT_APPROX(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ─── helper ─────────────────────────────────────────────────────────────── */

static double eval_expr(const char* expr, cxpr_context* ctx, cxpr_registry* reg,
                        cxpr_error* out_err) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) { if (out_err) *out_err = err; cxpr_parser_free(p); return NAN; }
    double result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    if (out_err) *out_err = err;
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static bool eval_bool_expr(const char* expr, cxpr_context* ctx, cxpr_registry* reg,
                           cxpr_error* out_err) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        if (out_err) *out_err = err;
        cxpr_parser_free(p);
        return false;
    }
    bool result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    if (out_err) *out_err = err;
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

/* ─── tests ──────────────────────────────────────────────────────────────── */

static void test_scalar_params(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "sum(a, b) => a + b");
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 7.0);

    double r = eval_expr("sum(x, y)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 10.0);

    /* With literals */
    r = eval_expr("sum(1.5, 2.5)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 4.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_struct_params_distance3(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_error err = cxpr_registry_define_fn(reg,
        "distance3(goal, pose) => "
        "sqrt((goal.x-pose.x)^2 + (goal.y-pose.y)^2 + (goal.z-pose.z)^2)");
    assert(err.code == CXPR_OK);

    cxpr_context* ctx = cxpr_context_new();
    const char* fields[] = {"x", "y", "z"};
    double goal_vals[] = {1.0, 2.0, 3.0};
    double pose_vals[] = {4.0, 6.0, 3.0}; /* distance = sqrt(9+16+0) = 5 */
    cxpr_context_set_fields(ctx, "goal", fields, goal_vals, 3);
    cxpr_context_set_fields(ctx, "pose", fields, pose_vals, 3);

    double d = eval_expr("distance3(goal, pose)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(d, 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_struct_params_dot2(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_error err = cxpr_registry_define_fn(reg, "dot2(u, v) => u.x*v.x + u.y*v.y");
    assert(err.code == CXPR_OK);

    cxpr_context* ctx = cxpr_context_new();
    const char* xy[] = {"x", "y"};
    double u_vals[] = {3.0, 4.0};
    double v_vals[] = {1.0, 0.0};
    cxpr_context_set_fields(ctx, "u", xy, u_vals, 2);
    cxpr_context_set_fields(ctx, "v", xy, v_vals, 2);

    double r = eval_expr("dot2(u, v)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 3.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_struct_with_dollar_params(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_error err = cxpr_registry_define_fn(reg,
        "clamp_val(p) => clamp(p.value, $lo, $hi)");
    assert(err.code == CXPR_OK);

    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set(ctx, "sensor.value", 1.5);
    cxpr_context_set_param(ctx, "lo", 0.0);
    cxpr_context_set_param(ctx, "hi", 1.0);

    double r = eval_expr("clamp_val(sensor)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 1.0); /* clamped to hi */

    cxpr_context_set(ctx, "sensor.value", -0.5);
    r = eval_expr("clamp_val(sensor)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 0.0); /* clamped to lo */

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_defined_fn_calls_defined_fn(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_error err = cxpr_registry_define_fn(reg, "sq(x) => x * x");
    assert(err.code == CXPR_OK);
    err = cxpr_registry_define_fn(reg, "hyp(a, b) => sqrt(sq(a) + sq(b))");
    assert(err.code == CXPR_OK);

    cxpr_context* ctx = cxpr_context_new();
    double r = eval_expr("hyp(3, 4)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_defined_fn_used_in_comparison(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_error err = cxpr_registry_define_fn(reg,
        "dist2(a, b) => sqrt((a.x-b.x)^2 + (a.y-b.y)^2)");
    assert(err.code == CXPR_OK);

    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set(ctx, "p.x", 0.0); cxpr_context_set(ctx, "p.y", 0.0);
    cxpr_context_set(ctx, "q.x", 3.0); cxpr_context_set(ctx, "q.y", 4.0);
    cxpr_context_set_param(ctx, "r", 10.0);

    bool result = eval_bool_expr("dist2(p, q) < $r", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true); /* 5 < 10 */

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_overwrite(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "double_it(x) => x * 2");
    assert(err.code == CXPR_OK);
    cxpr_context_set(ctx, "v", 5.0);
    double r = eval_expr("double_it(v)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 10.0);

    /* Overwrite with triple */
    err = cxpr_registry_define_fn(reg, "double_it(x) => x * 3");
    assert(err.code == CXPR_OK);
    r = eval_expr("double_it(v)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(r, 15.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_bad_body_syntax(void) {
    cxpr_registry* reg = cxpr_registry_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "bad(x) => x +* y");
    assert(err.code != CXPR_OK);

    cxpr_registry_free(reg);
}

static void test_error_missing_arrow(void) {
    cxpr_registry* reg = cxpr_registry_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "f(x) x + 1");
    assert(err.code == CXPR_ERR_SYNTAX);

    cxpr_registry_free(reg);
}

static void test_error_wrong_arity_at_call(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "add(a, b) => a + b");
    assert(err.code == CXPR_OK);

    double r = eval_expr("add(1)", ctx, reg, &err);
    assert(isnan(r));
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_struct_arg_not_identifier(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "len2(v) => sqrt(v.x^2 + v.y^2)");
    assert(err.code == CXPR_OK);

    /* Passing a number literal where a struct identifier is required */
    double r = eval_expr("len2(1.0)", ctx, reg, &err);
    assert(isnan(r));
    assert(err.code == CXPR_ERR_SYNTAX);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_missing_field_in_context(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();

    cxpr_error err = cxpr_registry_define_fn(reg, "len2(v) => sqrt(v.x^2 + v.y^2)");
    assert(err.code == CXPR_OK);

    cxpr_context_set(ctx, "p.x", 3.0); /* only x, no y */

    double r = eval_expr("len2(p)", ctx, reg, &err);
    assert(isnan(r));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_registry_lookup_arity(void) {
    cxpr_registry* reg = cxpr_registry_new();

    cxpr_registry_define_fn(reg, "f(a, b, c) => a + b + c");

    size_t min_args, max_args;
    bool found = cxpr_registry_lookup(reg, "f", &min_args, &max_args);
    assert(found);
    assert(min_args == 3);
    assert(max_args == 3);

    cxpr_registry_free(reg);
}

/* ─── main ───────────────────────────────────────────────────────────────── */

int main(void) {
    test_scalar_params();
    test_struct_params_distance3();
    test_struct_params_dot2();
    test_struct_with_dollar_params();
    test_defined_fn_calls_defined_fn();
    test_defined_fn_used_in_comparison();
    test_overwrite();
    test_error_bad_body_syntax();
    test_error_missing_arrow();
    test_error_wrong_arity_at_call();
    test_error_struct_arg_not_identifier();
    test_error_missing_field_in_context();
    test_registry_lookup_arity();

    printf("All define tests passed.\n");
    return 0;
}
