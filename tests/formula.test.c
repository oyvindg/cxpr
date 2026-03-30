/**
 * @file formula.test.c
 * @brief Unit tests for the formula engine.
 *
 * Tests covered:
 * - Single formula evaluation
 * - Multi-formula with dependency resolution
 * - Topological sort order
 * - Circular dependency detection
 * - External variable usage
 * - Evaluation order API
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: single formula
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_single_formula(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 10.0);
    assert(cxpr_formula_add(engine, "result", "x + 1", &err));
    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    double val = cxpr_formula_get(engine, "result", &found);
    assert(found);
    ASSERT_DOUBLE_EQ(val, 11.0);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_single_formula\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: multi-formula with dependencies
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_multi_formula_dependencies(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 10.0);

    /* a = x + 1 = 11 */
    assert(cxpr_formula_add(engine, "a", "x + 1", &err));
    /* b = a * 2 = 22 (depends on a) */
    assert(cxpr_formula_add(engine, "b", "a * 2", &err));
    /* c = a + b = 33 (depends on a and b) */
    assert(cxpr_formula_add(engine, "c", "a + b", &err));

    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "a", &found), 11.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "b", &found), 22.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "c", &found), 33.0);
    assert(found);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_multi_formula_dependencies\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: evaluation order
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_evaluation_order(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_error err = {0};

    /* Add in reverse order — engine should still resolve correctly */
    assert(cxpr_formula_add(engine, "c", "a + b", &err));  /* depends on a, b */
    assert(cxpr_formula_add(engine, "b", "a * 2", &err));  /* depends on a */
    assert(cxpr_formula_add(engine, "a", "x + 1", &err));  /* depends on x (external) */

    assert(cxpr_formula_compile(engine, &err));

    const char* names[10];
    size_t count = cxpr_formula_eval_order(engine, names, 10);
    assert(count == 3);

    /* a must come before b and c */
    size_t pos_a = 999, pos_b = 999, pos_c = 999;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], "a") == 0) pos_a = i;
        if (strcmp(names[i], "b") == 0) pos_b = i;
        if (strcmp(names[i], "c") == 0) pos_c = i;
    }
    assert(pos_a < pos_b);
    assert(pos_a < pos_c);
    assert(pos_b < pos_c); /* b before c since c depends on b */

    cxpr_formula_engine_free(engine);
    cxpr_registry_free(reg);
    printf("  ✓ test_evaluation_order\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: circular dependency detection
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_circular_dependency(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_error err = {0};

    assert(cxpr_formula_add(engine, "a", "b + 1", &err));
    assert(cxpr_formula_add(engine, "b", "c + 1", &err));
    assert(cxpr_formula_add(engine, "c", "a + 1", &err));

    bool ok = cxpr_formula_compile(engine, &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    cxpr_formula_engine_free(engine);
    cxpr_registry_free(reg);
    printf("  ✓ test_circular_dependency\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: self-reference
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_self_reference(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_error err = {0};

    assert(cxpr_formula_add(engine, "a", "a + 1", &err));

    bool ok = cxpr_formula_compile(engine, &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    cxpr_formula_engine_free(engine);
    cxpr_registry_free(reg);
    printf("  ✓ test_self_reference\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: no dependencies (all independent)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_independent_formulas(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 1.0);
    cxpr_context_set(ctx, "y", 2.0);
    cxpr_context_set(ctx, "z", 3.0);

    assert(cxpr_formula_add(engine, "a", "x * 10", &err));
    assert(cxpr_formula_add(engine, "b", "y * 10", &err));
    assert(cxpr_formula_add(engine, "c", "z * 10", &err));

    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "a", &found), 10.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "b", &found), 20.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "c", &found), 30.0);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_independent_formulas\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: formula with functions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_formula_with_functions(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", -5.0);

    assert(cxpr_formula_add(engine, "pos_x", "abs(x)", &err));
    assert(cxpr_formula_add(engine, "result", "sqrt(pos_x * 5)", &err));

    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "pos_x", &found), 5.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "result", &found), 5.0);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_with_functions\n");
}

static void test_formula_bool_result_type_mismatch(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 2.0);
    assert(cxpr_formula_add(engine, "flag", "x > 0", &err));
    assert(cxpr_formula_compile(engine, &err));

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_bool_result_type_mismatch\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: get non-existent formula
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_get_nonexistent(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_error err = {0};

    assert(cxpr_formula_add(engine, "a", "1 + 2", &err));
    assert(cxpr_formula_compile(engine, &err));

    bool found;
    cxpr_formula_get(engine, "nonexistent", &found);
    assert(!found);

    cxpr_formula_engine_free(engine);
    cxpr_registry_free(reg);
    printf("  ✓ test_get_nonexistent\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running formula tests...\n");
    test_single_formula();
    test_multi_formula_dependencies();
    test_evaluation_order();
    test_circular_dependency();
    test_self_reference();
    test_independent_formulas();
    test_formula_with_functions();
    test_formula_bool_result_type_mismatch();
    test_get_nonexistent();
    printf("All formula tests passed!\n");
    return 0;
}
