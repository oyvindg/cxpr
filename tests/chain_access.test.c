/**
 * Tests for Phase 2: chained field access (a.b.c and deeper).
 * Parser emits CXPR_NODE_CHAIN_ACCESS for depth > 2.
 * CXPR_NODE_FIELD_ACCESS is still used for depth == 2 (regression guard).
 *
 * All tests are expected to FAIL TO COMPILE until the Phase 2 implementation
 * is complete.
 */
#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "cxpr_test_internal.h"

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ── helpers ─────────────────────────────────────────────────────────── */

static cxpr_value eval_typed(const char *expr,
                                   cxpr_context *ctx, cxpr_registry *reg) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_value result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_value eval_typed_fails(const char *expr,
                                         cxpr_context *ctx, cxpr_registry *reg,
                                         cxpr_error_code expected) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_value result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == expected);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

/* Build context:
 *   outer.inner.value  = double 7.0
 *   outer.inner.flag   = bool   true
 *   outer.inner.sub.deep = double 99.0
 */
static cxpr_context *make_nested_ctx(void) {
    /* deepest: {deep: 99.0} */
    const char *deep_names[] = {"deep"};
    cxpr_value deep_vals[] = {cxpr_fv_double(99.0)};
    cxpr_struct_value *deep = cxpr_struct_value_new(deep_names, deep_vals, 1);

    /* inner: {value: 7.0, flag: true, sub: deep} */
    const char *inner_names[] = {"value", "flag", "sub"};
    cxpr_value inner_vals[] = {
        cxpr_fv_double(7.0), cxpr_fv_bool(true), cxpr_fv_struct(deep)};
    cxpr_struct_value *inner = cxpr_struct_value_new(inner_names, inner_vals, 3);
    cxpr_struct_value_free(deep);

    /* outer: {inner: inner} */
    const char *outer_names[] = {"inner"};
    cxpr_value outer_vals[] = {cxpr_fv_struct(inner)};
    cxpr_struct_value *outer = cxpr_struct_value_new(outer_names, outer_vals, 1);
    cxpr_struct_value_free(inner);

    cxpr_context *ctx = cxpr_context_new();
    cxpr_context_set_struct(ctx, "outer", outer);
    cxpr_struct_value_free(outer);
    return ctx;
}

/* ── two-segment access still emits FIELD_ACCESS (regression) ─────────── */

static void test_two_segment_is_field_access(void) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, "a.b", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_FIELD_ACCESS);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  \u2713 test_two_segment_is_field_access\n");
}

/* ── three-segment emits CHAIN_ACCESS ────────────────────────────────── */

static void test_three_segment_is_chain_access(void) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, "outer.inner.value", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_CHAIN_ACCESS);
    assert(cxpr_ast_chain_depth(ast) == 3);
    assert(strcmp(cxpr_ast_chain_segment(ast, 0), "outer") == 0);
    assert(strcmp(cxpr_ast_chain_segment(ast, 1), "inner") == 0);
    assert(strcmp(cxpr_ast_chain_segment(ast, 2), "value") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  \u2713 test_three_segment_is_chain_access\n");
}

/* ── chain resolves to double ─────────────────────────────────────────── */

static void test_chain_double(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    cxpr_value r = eval_typed("outer.inner.value", ctx, reg);
    assert(r.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(r.d, 7.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_chain_double\n");
}

/* ── chain resolves to bool, no coercion ─────────────────────────────── */

static void test_chain_bool(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    cxpr_value r = eval_typed("outer.inner.flag", ctx, reg);
    assert(r.type == CXPR_VALUE_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_chain_bool\n");
}

/* ── four-segment (three levels of nesting) ──────────────────────────── */

static void test_chain_four_segments(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    cxpr_value r = eval_typed("outer.inner.sub.deep", ctx, reg);
    assert(r.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(r.d, 99.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_chain_four_segments\n");
}

/* ── non-struct intermediate gives TYPE_MISMATCH ─────────────────────── */

static void test_non_struct_intermediate(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    /* outer.inner.value is double — cannot descend further */
    eval_typed_fails("outer.inner.value.extra", ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_non_struct_intermediate\n");
}

/* ── unknown intermediate field gives UNKNOWN_IDENTIFIER ─────────────── */

static void test_unknown_intermediate(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    eval_typed_fails("outer.missing.value", ctx, reg, CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_unknown_intermediate\n");
}

/* ── cxpr_ast_references returns full dotted path ────────────────────── */

static void test_chain_ast_references(void) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, "outer.inner.value", &err);
    assert(ast != NULL && err.code == CXPR_OK);

    const char *refs[4];
    size_t n = cxpr_ast_references(ast, refs, 4);
    assert(n == 1);
    assert(strcmp(refs[0], "outer.inner.value") == 0);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  \u2713 test_chain_ast_references\n");
}

/* ── chain used in a larger expression ───────────────────────────────── */

static void test_chain_in_expression(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = make_nested_ctx();

    /* outer.inner.value (double 7.0) > 5.0 → bool true */
    cxpr_value r = eval_typed("outer.inner.value > 5.0", ctx, reg);
    assert(r.type == CXPR_VALUE_BOOL);
    assert(r.b == true);

    /* outer.inner.flag (bool) && outer.inner.value > 5.0 (bool) → bool */
    r = eval_typed("outer.inner.flag && outer.inner.value > 5.0", ctx, reg);
    assert(r.type == CXPR_VALUE_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_chain_in_expression\n");
}

int main(void) {
    printf("Running chain_access tests...\n");
    test_two_segment_is_field_access();
    test_three_segment_is_chain_access();
    test_chain_double();
    test_chain_bool();
    test_chain_four_segments();
    test_non_struct_intermediate();
    test_unknown_intermediate();
    test_chain_ast_references();
    test_chain_in_expression();
    printf("All chain_access tests passed!\n");
    return 0;
}
