/**
 * Tests for Phase 1: native struct storage in cxpr_context.
 * cxpr_context_set_struct, cxpr_context_get_struct, cxpr_context_get_field,
 * field access in expressions, context lifecycle with structs.
 *
 * All tests are expected to FAIL TO COMPILE until the Phase 1 implementation
 * is complete (and Phase 0 typed eval is in place).
 */
#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
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

/* ── double field ────────────────────────────────────────────────────── */

static void test_double_field(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *names[] = {"x", "y", "z"};
    cxpr_value vals[] = {
        cxpr_fv_double(1.0), cxpr_fv_double(2.0), cxpr_fv_double(3.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 3);
    cxpr_context_set_struct(ctx, "pos", s);
    cxpr_struct_value_free(s);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(ctx, "pos", "x", &found);
    assert(found);
    assert(fv.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(fv.d, 1.0);

    cxpr_value r = eval_typed("pos.x + pos.y", ctx, reg);
    assert(r.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(r.d, 3.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_double_field\n");
}

/* ── bool field stays bool, no coercion ──────────────────────────────── */

static void test_bool_field(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *names[] = {"active", "value"};
    cxpr_value vals[] = {cxpr_fv_bool(true), cxpr_fv_double(5.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 2);
    cxpr_context_set_struct(ctx, "sensor", s);
    cxpr_struct_value_free(s);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(ctx, "sensor", "active", &found);
    assert(found);
    assert(fv.type == CXPR_VALUE_BOOL);
    assert(fv.b == true);

    /* sensor.active (bool) && x > 0.0 (bool) → bool */
    cxpr_context_set(ctx, "x", 1.0);
    cxpr_value r = eval_typed("sensor.active && x > 0.0", ctx, reg);
    assert(r.type == CXPR_VALUE_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_bool_field\n");
}

/* ── nested struct field: get_field returns STRUCT; scalar use is error ─ */

static void test_nested_struct_field(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *inner_names[] = {"z"};
    cxpr_value inner_vals[] = {cxpr_fv_double(9.0)};
    cxpr_struct_value *inner = cxpr_struct_value_new(inner_names, inner_vals, 1);

    const char *outer_names[] = {"inner"};
    cxpr_value outer_vals[] = {cxpr_fv_struct(inner)};
    cxpr_struct_value *outer = cxpr_struct_value_new(outer_names, outer_vals, 1);
    cxpr_struct_value_free(inner);

    cxpr_context_set_struct(ctx, "outer", outer);
    cxpr_struct_value_free(outer);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(ctx, "outer", "inner", &found);
    assert(found);
    assert(fv.type == CXPR_VALUE_STRUCT);

    /* using a struct field directly as a scalar is a type error */
    eval_typed_fails("outer.inner > 0.0", ctx, reg, CXPR_ERR_TYPE_MISMATCH);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_nested_struct_field\n");
}

/* ── set_struct replaces an existing entry ───────────────────────────── */

static void test_set_struct_replaces(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *names[] = {"v"};

    cxpr_value vals1[] = {cxpr_fv_double(1.0)};
    cxpr_struct_value *s1 = cxpr_struct_value_new(names, vals1, 1);
    cxpr_context_set_struct(ctx, "obj", s1);
    cxpr_struct_value_free(s1);

    cxpr_value vals2[] = {cxpr_fv_double(99.0)};
    cxpr_struct_value *s2 = cxpr_struct_value_new(names, vals2, 1);
    cxpr_context_set_struct(ctx, "obj", s2);
    cxpr_struct_value_free(s2);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(ctx, "obj", "v", &found);
    assert(found);
    assert(fv.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(fv.d, 99.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_set_struct_replaces\n");
}

/* ── context_clear removes structs ──────────────────────────────────── */

static void test_context_clear(void) {
    cxpr_context *ctx = cxpr_context_new();

    const char *names[] = {"v"};
    cxpr_value vals[] = {cxpr_fv_double(1.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 1);
    cxpr_context_set_struct(ctx, "obj", s);
    cxpr_struct_value_free(s);

    cxpr_context_clear(ctx);

    bool found = true;
    cxpr_context_get_field(ctx, "obj", "v", &found);
    assert(!found);

    cxpr_context_free(ctx);
    printf("  \u2713 test_context_clear\n");
}

/* ── context_clone deep-copies structs ───────────────────────────────── */

static void test_context_clone(void) {
    cxpr_context *ctx = cxpr_context_new();

    const char *names[] = {"v"};
    cxpr_value vals[] = {cxpr_fv_double(5.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 1);
    cxpr_context_set_struct(ctx, "obj", s);
    cxpr_struct_value_free(s);

    cxpr_context *clone = cxpr_context_clone(ctx);

    /* overwrite in original — clone must be unaffected */
    cxpr_value vals2[] = {cxpr_fv_double(99.0)};
    cxpr_struct_value *s2 = cxpr_struct_value_new(names, vals2, 1);
    cxpr_context_set_struct(ctx, "obj", s2);
    cxpr_struct_value_free(s2);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(clone, "obj", "v", &found);
    assert(found);
    ASSERT_DOUBLE_EQ(fv.d, 5.0);

    cxpr_context_free(ctx);
    cxpr_context_free(clone);
    printf("  \u2713 test_context_clone\n");
}

/* ── overlay context: field found in parent when not in child ─────────── */

static void test_parent_walk(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *parent = cxpr_context_new();

    const char *names[] = {"v"};
    cxpr_value vals[] = {cxpr_fv_double(42.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(names, vals, 1);
    cxpr_context_set_struct(parent, "obj", s);
    cxpr_struct_value_free(s);

    /* child has no "obj" struct — should fall through to parent */
    cxpr_context *child = cxpr_context_overlay_new(parent);

    bool found = false;
    cxpr_value fv = cxpr_context_get_field(child, "obj", "v", &found);
    assert(found);
    ASSERT_DOUBLE_EQ(fv.d, 42.0);

    /* expression via child context also resolves from parent */
    cxpr_value r = eval_typed("obj.v", child, reg);
    assert(r.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(r.d, 42.0);

    cxpr_context_free(child);
    cxpr_context_free(parent);
    cxpr_registry_free(reg);
    printf("  \u2713 test_parent_walk\n");
}

/* ── flat-key fallback (deprecated backward compat) ─────────────────── */

static void test_flat_key_fallback(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_context *ctx = cxpr_context_new();

    /* old API: flat keys like "pos.x" */
    const char *fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};
    cxpr_context_set_fields(ctx, "pos", fields, values, 2);

    /* expression must still resolve via flat-key fallback */
    cxpr_value r = eval_typed("pos.x + pos.y", ctx, reg);
    assert(r.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(r.d, 7.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_flat_key_fallback\n");
}

int main(void) {
    printf("Running struct_ctx tests...\n");
    test_double_field();
    test_bool_field();
    test_nested_struct_field();
    test_set_struct_replaces();
    test_context_clear();
    test_context_clone();
    test_parent_walk();
    test_flat_key_fallback();
    printf("All struct_ctx tests passed!\n");
    return 0;
}
