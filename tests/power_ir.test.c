/**
 * @file power_ir.test.c
 * @brief Regression: the `^`/`**` power operator must compile (not just
 *        constant-fold) so it works on runtime operands in the compiled
 *        program and the named-expression evaluator, matching the tree-walk
 *        evaluator.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define APPROX(a, b) (fabs((a) - (b)) < 1e-9)

static void test_compiled_program_power(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);

    const char* exprs[] = { "x^2", "2^y", "x^y", "x^2 + 2^3" };
    const double want[] = { 9.0, 16.0, 81.0, 17.0 };

    for (size_t i = 0; i < 4; ++i) {
        cxpr_ast* ast = cxpr_parse(p, exprs[i], &err);
        assert(ast && err.code == CXPR_OK);

        double tree = 0.0;
        assert(cxpr_eval_ast_number(ast, ctx, reg, &tree, &err) && err.code == CXPR_OK);

        cxpr_program* prog = cxpr_compile(ast, reg, &err);
        assert(prog && err.code == CXPR_OK); /* used to fail: rejected POWER */
        double compiled = 0.0;
        assert(cxpr_eval_program_number(prog, ctx, reg, &compiled, &err) && err.code == CXPR_OK);

        assert(APPROX(tree, want[i]));
        assert(APPROX(compiled, want[i]));

        cxpr_program_free(prog);
        cxpr_ast_free(ast);
    }

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  compiled-program power OK\n");
}

static void test_evaluator_power(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* ev = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_defaults(reg);
    cxpr_context_set(ctx, "radius", 2.0);

    const cxpr_expression_def defs[] = {
        { "area",   "3.14159265358979323846 * radius^2" },
        { "volume", "radius^3" },
    };
    assert(cxpr_expressions_add(ev, defs, 2, &err));
    assert(cxpr_evaluator_compile(ev, &err)); /* used to fail on radius^2 */
    cxpr_evaluator_eval(ev, ctx, &err);
    assert(err.code == CXPR_OK);

    assert(APPROX(cxpr_expression_get_double(ev, "area", NULL),
                  3.14159265358979323846 * 4.0));
    assert(APPROX(cxpr_expression_get_double(ev, "volume", NULL), 8.0));

    cxpr_context_free(ctx);
    cxpr_evaluator_free(ev);
    cxpr_registry_free(reg);
    printf("  evaluator power OK\n");
}

int main(void) {
    printf("power_ir tests:\n");
    test_compiled_program_power();
    test_evaluator_power();
    printf("All power_ir tests passed.\n");
    return 0;
}
