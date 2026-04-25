/**
 * @file expression.test.c
 * @brief Unit tests for the expression evaluator.
 *
 * Tests covered:
 * - Single expression evaluation
 * - Multi-expression with dependency resolution
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
                                        cxpr_value* out, size_t field_count,
                                        void* userdata) {
    (void)argc;
    (void)field_count;
    (void)userdata;
    out[0] = cxpr_fv_double(args[0]);
    out[1] = cxpr_fv_double(args[0] * 2.0);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: single expression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_single_formula(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 10.0);
    assert(cxpr_expression_add(evaluator, "result", "x + 1", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    double val = cxpr_expression_get_double(evaluator, "result", &found);
    assert(found);
    ASSERT_DOUBLE_EQ(val, 11.0);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_single_formula\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: multi-expression with dependencies
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_multi_formula_dependencies(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 10.0);

    /* a = x + 1 = 11 */
    assert(cxpr_expression_add(evaluator, "a", "x + 1", &err));
    /* b = a * 2 = 22 (depends on a) */
    assert(cxpr_expression_add(evaluator, "b", "a * 2", &err));
    /* c = a + b = 33 (depends on a and b) */
    assert(cxpr_expression_add(evaluator, "c", "a + b", &err));

    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "a", &found), 11.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "b", &found), 22.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "c", &found), 33.0);
    assert(found);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_multi_formula_dependencies\n");
}

static void test_formula_batch_add(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    const cxpr_expression_def defs[] = {
        { "a", "x + 1" },
        { "b", "a * 2" },
        { "c", "a + b" }
    };

    cxpr_context_set(ctx, "x", 10.0);

    assert(cxpr_expressions_add(evaluator, defs, 3, &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "a", &found), 11.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "b", &found), 22.0);
    assert(found);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "c", &found), 33.0);
    assert(found);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_batch_add\n");
}

static void test_formula_batch_add_rolls_back_on_error(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    const cxpr_expression_def defs[] = {
        { "good", "x + 1" },
        { "bad", "3 +" }
    };

    cxpr_context_set(ctx, "x", 10.0);

    assert(cxpr_expression_add(evaluator, "baseline", "x * 3", &err));
    assert(!cxpr_expressions_add(evaluator, defs, 2, &err));
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "baseline", &found), 30.0);
    assert(found);
    assert(cxpr_expression_get_double(evaluator, "good", &found) == 0.0);
    assert(!found);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_batch_add_rolls_back_on_error\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: evaluation order
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_evaluation_order(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    /* Add in reverse order — evaluator should still resolve correctly */
    assert(cxpr_expression_add(evaluator, "c", "a + b", &err));  /* depends on a, b */
    assert(cxpr_expression_add(evaluator, "b", "a * 2", &err));  /* depends on a */
    assert(cxpr_expression_add(evaluator, "a", "x + 1", &err));  /* depends on x (external) */

    assert(cxpr_evaluator_compile(evaluator, &err));

    const char* names[10];
    size_t count = cxpr_expression_eval_order(evaluator, names, 10);
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

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_evaluation_order\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: circular dependency detection
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_circular_dependency(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "a", "b + 1", &err));
    assert(cxpr_expression_add(evaluator, "b", "c + 1", &err));
    assert(cxpr_expression_add(evaluator, "c", "a + 1", &err));

    bool ok = cxpr_evaluator_compile(evaluator, &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_circular_dependency\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: self-reference
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_self_reference(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "a", "a + 1", &err));

    bool ok = cxpr_evaluator_compile(evaluator, &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_self_reference\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: no dependencies (all independent)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_independent_formulas(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 1.0);
    cxpr_context_set(ctx, "y", 2.0);
    cxpr_context_set(ctx, "z", 3.0);

    assert(cxpr_expression_add(evaluator, "a", "x * 10", &err));
    assert(cxpr_expression_add(evaluator, "b", "y * 10", &err));
    assert(cxpr_expression_add(evaluator, "c", "z * 10", &err));

    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "a", &found), 10.0);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "b", &found), 20.0);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "c", &found), 30.0);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_independent_formulas\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: expression with functions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_formula_with_functions(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", -5.0);

    assert(cxpr_expression_add(evaluator, "pos_x", "abs(x)", &err));
    assert(cxpr_expression_add(evaluator, "result", "sqrt(pos_x * 5)", &err));

    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "pos_x", &found), 5.0);
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "result", &found), 5.0);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_with_functions\n");
}

static void test_formula_bool_result_is_typed(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    cxpr_context_set(ctx, "x", 2.0);
    assert(cxpr_expression_add(evaluator, "flag", "x > 0", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    cxpr_value flag = cxpr_expression_get(evaluator, "flag", &found);
    assert(found == true);
    assert(flag.type == CXPR_VALUE_BOOL);
    assert(flag.b == true);
    assert(cxpr_expression_get_bool(evaluator, "flag", &found) == true);
    assert(found == true);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_bool_result_is_typed\n");
}

static void test_analyze_formulas_reports_dependencies(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis analyses[3];
    size_t order[3] = {0};
    cxpr_error err = {0};
    const cxpr_expression_def defs[] = {
        { "signal", "trend and rsi > 50" },
        { "entry", "signal and close > 100" },
        { "trend", "ema_fast > ema_slow" }
    };

    cxpr_register_defaults(reg);
    assert(cxpr_analyze_expressions(defs, 3, reg, analyses, order, &err));
    assert(err.code == CXPR_OK);
    assert(analyses[0].uses_expressions == true);
    assert(analyses[1].uses_expressions == true);
    assert(analyses[2].uses_expressions == false);
    assert(order[2] == 1);

    size_t pos_trend = 999, pos_signal = 999, pos_entry = 999;
    for (size_t i = 0; i < 3; i++) {
        if (order[i] == 0) pos_signal = i;
        if (order[i] == 1) pos_entry = i;
        if (order[i] == 2) pos_trend = i;
    }
    assert(pos_trend < pos_signal);
    assert(pos_signal < pos_entry);

    cxpr_registry_free(reg);
    printf("  ✓ test_analyze_formulas_reports_dependencies\n");
}

static void test_analyze_formulas_detects_struct_field_dependency(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis analyses[2];
    size_t order[2] = {0};
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    const cxpr_expression_def defs[] = {
        { "sum", "p.x + p.y" },
        { "p", "point(3)" }
    };

    cxpr_registry_add_struct(reg, "point", test_formula_point_producer,
                             1, 1, fields, 2, NULL, NULL);
    assert(cxpr_analyze_expressions(defs, 2, reg, analyses, order, &err));
    assert(err.code == CXPR_OK);
    assert(analyses[0].uses_expressions == true);
    assert(analyses[0].uses_field_access == true);
    assert(order[0] == 1);
    assert(order[1] == 0);

    cxpr_registry_free(reg);
    printf("  ✓ test_analyze_formulas_detects_struct_field_dependency\n");
}

static void test_analyze_formulas_detects_cycles(void) {
    cxpr_analysis analyses[2];
    cxpr_error err = {0};
    const cxpr_expression_def defs[] = {
        { "a", "b + 1" },
        { "b", "a + 1" }
    };

    assert(!cxpr_analyze_expressions(defs, 2, NULL, analyses, NULL, &err));
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);
    printf("  ✓ test_analyze_formulas_detects_cycles\n");
}

static void test_formula_bool_dependency_stays_typed(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;

    cxpr_context_set(ctx, "x", 2.0);
    cxpr_context_set(ctx, "rsi", 55.0);

    assert(cxpr_expression_add(evaluator, "trend", "x > 0", &err));
    assert(cxpr_expression_add(evaluator, "entry", "trend and rsi > 50", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_bool(evaluator, "trend", &found) == true);
    assert(found == true);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) == true);
    assert(found == true);

    cxpr_context_set(ctx, "x", -1.0);
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_bool(evaluator, "trend", &found) == false);
    assert(found == true);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) == false);
    assert(found == true);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_bool_dependency_stays_typed\n");
}

static void test_formula_struct_result_and_field_dependency(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    const char* fields[] = {"x", "y"};

    cxpr_registry_add_struct(reg, "point", test_formula_point_producer,
                             1, 1, fields, 2, NULL, NULL);
    assert(cxpr_expression_add(evaluator, "p", "point(3)", &err));
    assert(cxpr_expression_add(evaluator, "sum", "p.x + p.y", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    cxpr_value p = cxpr_expression_get(evaluator, "p", &found);
    assert(found == true);
    assert(p.type == CXPR_VALUE_STRUCT);
    assert(p.s != NULL);
    assert(p.s->field_count == 2);
    assert(strcmp(p.s->field_names[0], "x") == 0);
    assert(strcmp(p.s->field_names[1], "y") == 0);
    ASSERT_DOUBLE_EQ(p.s->field_values[0].d, 3.0);
    ASSERT_DOUBLE_EQ(p.s->field_values[1].d, 6.0);

    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "sum", &found), 9.0);
    assert(found == true);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_struct_result_and_field_dependency\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: get non-existent expression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_get_nonexistent(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "a", "1 + 2", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    bool found;
    cxpr_expression_get(evaluator, "nonexistent", &found);
    assert(!found);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_get_nonexistent\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running expression tests...\n");
    test_single_formula();
    test_multi_formula_dependencies();
    test_formula_batch_add();
    test_formula_batch_add_rolls_back_on_error();
    test_evaluation_order();
    test_circular_dependency();
    test_self_reference();
    test_independent_formulas();
    test_formula_with_functions();
    test_analyze_formulas_reports_dependencies();
    test_analyze_formulas_detects_struct_field_dependency();
    test_analyze_formulas_detects_cycles();
    test_formula_bool_result_is_typed();
    test_formula_bool_dependency_stays_typed();
    test_formula_struct_result_and_field_dependency();
    test_get_nonexistent();
    printf("All expression tests passed!\n");
    return 0;
}
