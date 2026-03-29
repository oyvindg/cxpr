/**
 * @file formula_ir.test.c
 * @brief Internal tests for FormulaEngine compiled-program integration.
 */

#include "internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static void test_formula_compile_creates_programs(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    assert(cxpr_formula_add(engine, "base", "x + 1", &err) == true);
    assert(cxpr_formula_add(engine, "signal", "base * 2", &err) == true);
    assert(cxpr_formula_compile(engine, &err) == true);
    assert(err.code == CXPR_OK);

    assert(engine->formulas[0].program != NULL);
    assert(engine->formulas[1].program != NULL);

    cxpr_context_set(ctx, "x", 3.0);
    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found = false;
    ASSERT_DOUBLE_EQ(cxpr_formula_get(engine, "signal", &found), 8.0);
    assert(found == true);

    cxpr_context_free(ctx);
    cxpr_formula_engine_free(engine);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_compile_creates_programs\n");
}

int main(void) {
    printf("Running formula IR tests...\n");
    test_formula_compile_creates_programs();
    printf("All formula IR tests passed!\n");
    return 0;
}
