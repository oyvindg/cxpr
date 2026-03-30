/**
 * Tests for Phase 0: cxpr_field_value type system, bool literals, typed
 * evaluation API, and operator type rules.
 *
 * All tests are expected to FAIL TO COMPILE until the Phase 0 implementation
 * is complete.
 */
#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ── helpers ─────────────────────────────────────────────────────────── */

static cxpr_field_value eval_typed(const char *expr,
                                   cxpr_context *ctx, cxpr_registry *reg) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_field_value eval_typed_fails(const char *expr,
                                         cxpr_context *ctx, cxpr_registry *reg,
                                         cxpr_error_code expected) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == expected);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_field_value ir_eval_typed(const char *expr,
                                      cxpr_context *ctx, cxpr_registry *reg) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_program *prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ir_eval(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_program_free(prog);
    cxpr_parser_free(p);
    return result;
}

/* ── value constructors ──────────────────────────────────────────────── */

static void test_fv_constructors(void) {
    cxpr_field_value d = cxpr_fv_double(3.14);
    assert(d.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(d.d, 3.14);

    cxpr_field_value bt = cxpr_fv_bool(true);
    assert(bt.type == CXPR_FIELD_BOOL);
    assert(bt.b == true);

    cxpr_field_value bf = cxpr_fv_bool(false);
    assert(bf.type == CXPR_FIELD_BOOL);
    assert(bf.b == false);

    printf("  \u2713 test_fv_constructors\n");
}

/* ── cxpr_struct_value lifecycle ─────────────────────────────────────── */

static void test_struct_value_new_free(void) {
    const char *names[] = {"x", "y"};
    cxpr_field_value vals[] = {cxpr_fv_double(1.0), cxpr_fv_double(2.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 2);
    assert(s != NULL);
    assert(s->field_count == 2);
    assert(strcmp(s->field_names[0], "x") == 0);
    assert(strcmp(s->field_names[1], "y") == 0);
    assert(s->field_values[0].type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(s->field_values[0].d, 1.0);
    ASSERT_DOUBLE_EQ(s->field_values[1].d, 2.0);
    cxpr_struct_value_free(s);
    printf("  \u2713 test_struct_value_new_free\n");
}

static void test_struct_value_deep_copy(void) {
    const char *names[] = {"a"};
    cxpr_field_value vals[] = {cxpr_fv_double(42.0)};
    cxpr_struct_value *orig = cxpr_struct_value_new(names, vals, 1);
    assert(orig != NULL);

    cxpr_struct_value *copy = cxpr_struct_value_new(
        (const char *const *)orig->field_names, orig->field_values,
        orig->field_count);
    assert(copy != NULL);

    /* mutating original must not affect copy */
    orig->field_values[0].d = 99.0;
    ASSERT_DOUBLE_EQ(copy->field_values[0].d, 42.0);

    cxpr_struct_value_free(orig);
    cxpr_struct_value_free(copy);
    printf("  \u2713 test_struct_value_deep_copy\n");
}

static void test_struct_value_nested_deep_copy(void) {
    const char *inner_names[] = {"z"};
    cxpr_field_value inner_vals[] = {cxpr_fv_double(7.0)};
    cxpr_struct_value *inner = cxpr_struct_value_new(inner_names, inner_vals, 1);

    const char *outer_names[] = {"nested"};
    cxpr_field_value outer_vals[] = {cxpr_fv_struct(inner)};
    cxpr_struct_value *outer = cxpr_struct_value_new(outer_names, outer_vals, 1);
    assert(outer != NULL);
    assert(outer->field_values[0].type == CXPR_FIELD_STRUCT);

    /* mutating inner after outer was constructed must not affect outer's copy */
    inner->field_values[0].d = 99.0;
    ASSERT_DOUBLE_EQ(outer->field_values[0].s->field_values[0].d, 7.0);

    cxpr_struct_value_free(inner);
    cxpr_struct_value_free(outer);
    printf("  \u2713 test_struct_value_nested_deep_copy\n");
}

/* ── arithmetic operators produce double ─────────────────────────────── */

static void test_arithmetic_double(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = eval_typed("1.0 + 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 3.0);

    r = eval_typed("6.0 / 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 3.0);

    r = eval_typed("-5.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, -5.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_arithmetic_double\n");
}

/* ── comparison operators produce bool ───────────────────────────────── */

static void test_comparison_returns_bool(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = eval_typed("1.0 < 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("2.0 < 1.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == false);

    r = eval_typed("1.0 == 1.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("1.0 != 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("2.0 >= 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_comparison_returns_bool\n");
}

/* ── bool literals ───────────────────────────────────────────────────── */

static void test_bool_literals(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = eval_typed("true", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == false);

    r = eval_typed("true == true", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("true != false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_bool_literals\n");
}

/* ── logical operators require bool operands and return bool ─────────── */

static void test_logical_operators(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = eval_typed("true && false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == false);

    r = eval_typed("true || false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = eval_typed("!true", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == false);

    /* compound: comparison feeds logical */
    r = eval_typed("1.0 < 2.0 && 3.0 < 4.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_logical_operators\n");
}

/* ── ternary preserves branch type ──────────────────────────────────── */

static void test_ternary_typed(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = eval_typed("1.0 < 2.0 ? 10.0 : 20.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 10.0);

    r = eval_typed("2.0 < 1.0 ? 10.0 : 20.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 20.0);

    r = eval_typed("true ? true : false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_ternary_typed\n");
}

/* ── type errors ─────────────────────────────────────────────────────── */

static void test_type_errors(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    /* bool in arithmetic */
    eval_typed_fails("true + 1.0",  ctx, reg, CXPR_ERR_TYPE_MISMATCH);
    eval_typed_fails("true - 1.0",  ctx, reg, CXPR_ERR_TYPE_MISMATCH);
    eval_typed_fails("-true",       ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    /* double in logical */
    eval_typed_fails("1.0 && 0.0",  ctx, reg, CXPR_ERR_TYPE_MISMATCH);
    eval_typed_fails("1.0 || 0.0",  ctx, reg, CXPR_ERR_TYPE_MISMATCH);
    eval_typed_fails("!1.0",        ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    /* mixed types in equality */
    eval_typed_fails("true == 1.0", ctx, reg, CXPR_ERR_TYPE_MISMATCH);
    eval_typed_fails("1.0 != true", ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    /* non-bool ternary condition */
    eval_typed_fails("1.0 ? 2.0 : 3.0", ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_type_errors\n");
}

/* ── convenience wrappers ────────────────────────────────────────────── */

static void test_eval_double_wrapper(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    /* cxpr_ast_eval_double on a bool expression → NAN + TYPE_MISMATCH */
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, "1.0 < 2.0", &err);
    assert(ast != NULL);
    double d = cxpr_ast_eval_double(ast, ctx, reg, &err);
    assert(isnan(d));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    /* cxpr_ast_eval_double on a double expression → value, no error */
    p = cxpr_parser_new();
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "2.0 + 3.0", &err);
    assert(ast != NULL);
    d = cxpr_ast_eval_double(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(d, 5.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_eval_double_wrapper\n");
}

static void test_eval_bool_wrapper(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    /* cxpr_ast_eval_bool on a double expression → false + TYPE_MISMATCH */
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, "1.0 + 2.0", &err);
    assert(ast != NULL);
    bool b = cxpr_ast_eval_bool(ast, ctx, reg, &err);
    assert(b == false);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    /* cxpr_ast_eval_bool on a bool expression → value, no error */
    p = cxpr_parser_new();
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "1.0 < 2.0", &err);
    assert(ast != NULL);
    b = cxpr_ast_eval_bool(ast, ctx, reg, &err);
    assert(b == true);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_eval_bool_wrapper\n");
}

/* ── IR parity ───────────────────────────────────────────────────────── */

static void test_ir_parity(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    cxpr_field_value r;

    r = ir_eval_typed("1.0 + 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 3.0);

    r = ir_eval_typed("1.0 < 2.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = ir_eval_typed("true", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    r = ir_eval_typed("true && false", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == false);

    r = ir_eval_typed("1.0 < 2.0 ? 10.0 : 20.0", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 10.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_ir_parity\n");
}

int main(void) {
    printf("Running field_value tests...\n");
    test_fv_constructors();
    test_struct_value_new_free();
    test_struct_value_deep_copy();
    test_struct_value_nested_deep_copy();
    test_arithmetic_double();
    test_comparison_returns_bool();
    test_bool_literals();
    test_logical_operators();
    test_ternary_typed();
    test_type_errors();
    test_eval_double_wrapper();
    test_eval_bool_wrapper();
    test_ir_parity();
    printf("All field_value tests passed!\n");
    return 0;
}
