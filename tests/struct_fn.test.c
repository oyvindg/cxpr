/**
 * @file struct_fn.test.c
 * @brief Tests for cxpr_registry_add_fn — struct-aware function calls.
 *
 * Tests covered:
 * - distance3(goal, pose)  instead of  distance3(goal.x, goal.y, goal.z, pose.x, ...)
 * - Single-struct argument: vec3_length(v)
 * - Mixed: struct call inside arithmetic expression
 * - Error: wrong number of struct arguments
 * - Error: non-identifier passed as struct argument (field access, number, expression)
 * - Error: missing field in context
 * - Overwrite: replacing a plain function with a struct-aware one
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-9
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * Test helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static double eval_expr(const char* expr, cxpr_context* ctx, cxpr_registry* reg,
                        cxpr_error* out_err) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        if (out_err) *out_err = err;
        cxpr_parser_free(p);
        return NAN;
    }
    double result = cxpr_ast_eval_double(ast, ctx, reg, &err);
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
    bool result = cxpr_ast_eval_bool(ast, ctx, reg, &err);
    if (out_err) *out_err = err;
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * User-registered functions (same ABI as always — flat double* args)
 * ═══════════════════════════════════════════════════════════════════════════ */

static double fn_distance3(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    double dx = args[0] - args[3];
    double dy = args[1] - args[4];
    double dz = args[2] - args[5];
    return sqrt(dx*dx + dy*dy + dz*dz);
}

static double fn_vec3_length(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    return sqrt(args[0]*args[0] + args[1]*args[1] + args[2]*args[2]);
}

static double fn_dot3(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    return args[0]*args[3] + args[1]*args[4] + args[2]*args[5];
}

static void set_vec3_struct(cxpr_context* ctx, const char* name,
                            double x, double y, double z) {
    const char* fields[] = {"x", "y", "z"};
    cxpr_field_value values[] = {
        cxpr_fv_double(x),
        cxpr_fv_double(y),
        cxpr_fv_double(z),
    };
    cxpr_struct_value* s = cxpr_struct_value_new(fields, values, 3);
    assert(s != NULL);
    cxpr_context_set_struct(ctx, name, s);
    cxpr_struct_value_free(s);
}

static void set_vec2_struct(cxpr_context* ctx, const char* name,
                            double x, double y) {
    const char* fields[] = {"x", "y"};
    cxpr_field_value values[] = {
        cxpr_fv_double(x),
        cxpr_fv_double(y),
    };
    cxpr_struct_value* s = cxpr_struct_value_new(fields, values, 2);
    assert(s != NULL);
    cxpr_context_set_struct(ctx, name, s);
    cxpr_struct_value_free(s);
}

static void set_vec3_with_bool_y(cxpr_context* ctx, const char* name,
                                 double x, bool y, double z) {
    const char* fields[] = {"x", "y", "z"};
    cxpr_field_value values[] = {
        cxpr_fv_double(x),
        cxpr_fv_bool(y),
        cxpr_fv_double(z),
    };
    cxpr_struct_value* s = cxpr_struct_value_new(fields, values, 3);
    assert(s != NULL);
    cxpr_context_set_struct(ctx, name, s);
    cxpr_struct_value_free(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Tests
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_distance3_compact(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, xyz, 3, 2, NULL, NULL);

    /* goal = (1, 2, 3), pose = (4, 6, 3) → distance = sqrt(9+16+0) = 5 */
    set_vec3_struct(ctx, "goal", 1.0, 2.0, 3.0);
    set_vec3_struct(ctx, "pose", 4.0, 6.0, 3.0);

    cxpr_error err = {0};
    double d = eval_expr("distance3(goal, pose)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(d, 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_distance3_used_in_comparison(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, xyz, 3, 2, NULL, NULL);

    set_vec3_struct(ctx, "goal", 0.0, 0.0, 0.0);
    set_vec3_struct(ctx, "pose", 3.0, 4.0, 0.0);
    cxpr_context_set_param(ctx, "capture_radius", 10.0);

    cxpr_error err = {0};
    bool result = eval_bool_expr("distance3(goal, pose) < $capture_radius", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true); /* true: 5 < 10 */

    cxpr_context_set_param(ctx, "capture_radius", 3.0);
    result = eval_bool_expr("distance3(goal, pose) < $capture_radius", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == false); /* false: 5 >= 3 */

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_single_struct_arg(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "vec3_length", fn_vec3_length, xyz, 3, 1, NULL, NULL);

    set_vec3_struct(ctx, "v", 0.0, 3.0, 4.0);

    cxpr_error err = {0};
    double len = eval_expr("vec3_length(v)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(len, 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_struct_fn_in_arithmetic(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "vec3_length", fn_vec3_length, xyz, 3, 1, NULL, NULL);

    set_vec3_struct(ctx, "v", 0.0, 3.0, 4.0);

    cxpr_error err = {0};
    double result = eval_expr("vec3_length(v) * 2 + 1", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(result, 11.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_dot3_two_structs(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "dot3", fn_dot3, xyz, 3, 2, NULL, NULL);

    set_vec3_struct(ctx, "a", 1.0, 0.0, 0.0);
    set_vec3_struct(ctx, "b", 1.0, 0.0, 0.0);

    cxpr_error err = {0};
    double d = eval_expr("dot3(a, b)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(d, 1.0);

    /* Perpendicular vectors → dot = 0 */
    set_vec3_struct(ctx, "b", 0.0, 1.0, 0.0);
    d = eval_expr("dot3(a, b)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(d, 0.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_wrong_struct_argc(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, xyz, 3, 2, NULL, NULL);

    set_vec3_struct(ctx, "a", 1.0, 2.0, 3.0);

    /* Pass only 1 struct argument instead of 2 */
    cxpr_error err = {0};
    double result = eval_expr("distance3(a)", ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_non_identifier_struct_arg(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "vec3_length", fn_vec3_length, xyz, 3, 1, NULL, NULL);

    /* Passing a numeric literal — not an identifier */
    cxpr_error err = {0};
    double result = eval_expr("vec3_length(1.0)", ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_SYNTAX);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_error_missing_field_in_context(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "vec3_length", fn_vec3_length, xyz, 3, 1, NULL, NULL);

    /* Only set x and y — z is missing */
    set_vec2_struct(ctx, "v", 1.0, 2.0);

    cxpr_error err = {0};
    double result = eval_expr("vec3_length(v)", ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_overwrite_with_struct_fn(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    /* First register as plain 6-arg function */
    cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);

    /* Now overwrite with struct-aware version */
    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, xyz, 3, 2, NULL, NULL);

    set_vec3_struct(ctx, "goal", 0.0, 0.0, 0.0);
    set_vec3_struct(ctx, "pose", 0.0, 0.0, 5.0);

    cxpr_error err = {0};
    double d = eval_expr("distance3(goal, pose)", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(d, 5.0);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

static void test_registry_lookup_reports_struct_argc(void) {
    cxpr_registry* reg = cxpr_registry_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, xyz, 3, 2, NULL, NULL);

    size_t min_args, max_args;
    bool found = cxpr_registry_lookup(reg, "distance3", &min_args, &max_args);
    assert(found);
    assert(min_args == 2);
    assert(max_args == 2);

    cxpr_registry_free(reg);
}

static void test_error_bool_struct_field_type_mismatch(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx  = cxpr_context_new();

    const char* xyz[] = {"x", "y", "z"};
    cxpr_registry_add_fn(reg, "vec3_length", fn_vec3_length, xyz, 3, 1, NULL, NULL);
    set_vec3_with_bool_y(ctx, "v", 1.0, true, 3.0);

    cxpr_error err = {0};
    double result = eval_expr("vec3_length(v)", ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    test_distance3_compact();
    test_distance3_used_in_comparison();
    test_single_struct_arg();
    test_struct_fn_in_arithmetic();
    test_dot3_two_structs();
    test_error_wrong_struct_argc();
    test_error_non_identifier_struct_arg();
    test_error_missing_field_in_context();
    test_error_bool_struct_field_type_mismatch();
    test_overwrite_with_struct_fn();
    test_registry_lookup_reports_struct_argc();

    printf("All struct_fn tests passed.\n");
    return 0;
}
