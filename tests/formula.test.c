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
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static void test_formula_point_producer(const double* args, size_t argc,
                                        cxpr_field_value* out, size_t field_count,
                                        void* userdata) {
    (void)argc;
    (void)field_count;
    (void)userdata;
    out[0] = cxpr_fv_double(args[0]);
    out[1] = cxpr_fv_double(args[0] * 2.0);
}

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
    double val = cxpr_formula_get_double(engine, "result", &found);
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
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "a", &found), 11.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "b", &found), 22.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "c", &found), 33.0);
    assert(found);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_multi_formula_dependencies\n");
}

static void test_formula_batch_add(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    const cxpr_formula_def defs[] = {
        { "a", "x + 1" },
        { "b", "a * 2" },
        { "c", "a + b" }
    };

    cxpr_context_set(ctx, "x", 10.0);

    assert(cxpr_formulas_add(engine, defs, 3, &err));
    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "a", &found), 11.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "b", &found), 22.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "c", &found), 33.0);
    assert(found);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_batch_add\n");
}

static void test_formula_batch_add_rolls_back_on_error(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    const cxpr_formula_def defs[] = {
        { "good", "x + 1" },
        { "bad", "3 +" }
    };

    cxpr_context_set(ctx, "x", 10.0);

    assert(cxpr_formula_add(engine, "baseline", "x * 3", &err));
    assert(!cxpr_formulas_add(engine, defs, 2, &err));
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(cxpr_formula_compile(engine, &err));
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "baseline", &found), 30.0);
    assert(found);
    assert(cxpr_formula_get_double(engine, "good", &found) == 0.0);
    assert(!found);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_batch_add_rolls_back_on_error\n");
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
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "a", &found), 10.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "b", &found), 20.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "c", &found), 30.0);

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
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "pos_x", &found), 5.0);
    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "result", &found), 5.0);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_with_functions\n");
}

static void test_formula_bool_result_is_typed(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    cxpr_context_set(ctx, "x", 2.0);
    assert(cxpr_formula_add(engine, "flag", "x > 0", &err));
    assert(cxpr_formula_compile(engine, &err));

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);
    cxpr_field_value flag = cxpr_formula_get(engine, "flag", &found);
    assert(found == true);
    assert(flag.type == CXPR_FIELD_BOOL);
    assert(flag.b == true);
    assert(cxpr_formula_get_bool(engine, "flag", &found) == true);
    assert(found == true);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_bool_result_is_typed\n");
}

static void test_formula_bool_dependency_stays_typed(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    cxpr_context_set(ctx, "x", 2.0);
    cxpr_context_set(ctx, "rsi", 55.0);

    assert(cxpr_formula_add(engine, "trend", "x > 0", &err));
    assert(cxpr_formula_add(engine, "entry", "trend and rsi > 50", &err));
    assert(cxpr_formula_compile(engine, &err));

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_formula_get_bool(engine, "trend", &found) == true);
    assert(found == true);
    assert(cxpr_formula_get_bool(engine, "entry", &found) == true);
    assert(found == true);

    cxpr_context_set(ctx, "x", -1.0);
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_formula_get_bool(engine, "trend", &found) == false);
    assert(found == true);
    assert(cxpr_formula_get_bool(engine, "entry", &found) == false);
    assert(found == true);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_bool_dependency_stays_typed\n");
}

static void test_formula_struct_result_and_field_dependency(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    const char* fields[] = {"x", "y"};

    cxpr_registry_add_struct(reg, "point", test_formula_point_producer,
                             1, 1, fields, 2, NULL, NULL);
    assert(cxpr_formula_add(engine, "p", "point(3)", &err));
    assert(cxpr_formula_add(engine, "sum", "p.x + p.y", &err));
    assert(cxpr_formula_compile(engine, &err));

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    cxpr_field_value p = cxpr_formula_get(engine, "p", &found);
    assert(found == true);
    assert(p.type == CXPR_FIELD_STRUCT);
    assert(p.s != NULL);
    assert(p.s->field_count == 2);
    assert(strcmp(p.s->field_names[0], "x") == 0);
    assert(strcmp(p.s->field_names[1], "y") == 0);
    ASSERT_DOUBLE_EQ(p.s->field_values[0].d, 3.0);
    ASSERT_DOUBLE_EQ(p.s->field_values[1].d, 6.0);

    ASSERT_DOUBLE_EQ(cxpr_formula_get_double(engine, "sum", &found), 9.0);
    assert(found == true);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_struct_result_and_field_dependency\n");
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
    test_formula_batch_add();
    test_formula_batch_add_rolls_back_on_error();
    test_evaluation_order();
    test_circular_dependency();
    test_self_reference();
    test_independent_formulas();
    test_formula_with_functions();
    test_formula_bool_result_is_typed();
    test_formula_bool_dependency_stays_typed();
    test_formula_struct_result_and_field_dependency();
    test_get_nonexistent();
    printf("All formula tests passed!\n");
    return 0;
}
