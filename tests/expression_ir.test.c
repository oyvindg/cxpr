/**
 * @file expression_ir.test.c
 * @brief Internal tests for expression-evaluator compiled-program integration.
 */

#include "cxpr_test_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static void test_formula_compile_creates_programs(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "base", "x + 1", &err) == true);
    assert(cxpr_expression_add(evaluator, "signal", "base * 2", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == true);
    assert(err.code == CXPR_OK);

    assert(evaluator->expressions[0].program != NULL);
    assert(evaluator->expressions[1].program != NULL);

    cxpr_context_set(ctx, "x", 3.0);
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found = false;
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "signal", &found), 8.0);
    assert(found == true);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_compile_creates_programs\n");
}

static void test_formula_compile_unknown_function_fails(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "signal", "missing_fn(x) + 1", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == false);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    assert(strcmp(err.message, "Unknown function") == 0);
    assert(evaluator->compiled == false);
    assert(evaluator->expressions[0].program == NULL);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_compile_unknown_function_fails\n");
}

int main(void) {
    printf("Running expression IR tests...\n");
    test_formula_compile_creates_programs();
    test_formula_compile_unknown_function_fails();
    printf("All expression IR tests passed!\n");
    return 0;
}
