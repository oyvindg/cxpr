/**
 * @file membership.test.c
 * @brief Tests for `in` (set membership) and `within` (interval membership).
 *
 * `in [a, b, c]` desugars to an OR-chain of equalities, so it works for any
 * scalar type that equality supports (numbers, strings). `within [lo, hi]`
 * desugars to `lo <= x <= hi`. Both are checked through the tree-walk and
 * compiled paths.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static bool eval_bool_both(const char* expr, cxpr_context* ctx) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;
    bool tree_r = false;
    bool prog_r = false;

    cxpr_register_defaults(reg);
    ast = cxpr_parse(p, expr, &err);
    assert(ast && err.code == CXPR_OK);

    assert(cxpr_eval_ast_bool(ast, ctx, reg, &tree_r, &err) && err.code == CXPR_OK);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog && err.code == CXPR_OK);
    assert(cxpr_eval_program_bool(prog, ctx, reg, &prog_r, &err) && err.code == CXPR_OK);
    assert(tree_r == prog_r); /* engines must agree */

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    return tree_r;
}

static void test_numeric_set_membership(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set(ctx, "x", 20.0);

    assert(eval_bool_both("x in [10, 20, 30]", ctx) == true);
    assert(eval_bool_both("x in [10, 30]", ctx) == false);
    assert(eval_bool_both("x not in [10, 30]", ctx) == true);
    assert(eval_bool_both("x not in [10, 20, 30]", ctx) == false);
    assert(eval_bool_both("x in [20]", ctx) == true); /* single element */

    cxpr_context_free(ctx);
    printf("  numeric set membership OK\n");
}

static void test_string_set_membership(void) {
    /* The dynasty pattern: regime in [list of enum-like values]. */
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set_string(ctx, "region", "EU");

    assert(eval_bool_both("region in [\"US\", \"EU\", \"APAC\"]", ctx) == true);
    assert(eval_bool_both("region in [\"US\", \"APAC\"]", ctx) == false);
    assert(eval_bool_both("region not in [\"US\", \"APAC\"]", ctx) == true);

    cxpr_context_free(ctx);
    printf("  string set membership OK\n");
}

static void test_within_interval(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set(ctx, "x", 15.0);

    assert(eval_bool_both("x within [10, 20]", ctx) == true);
    assert(eval_bool_both("x within [16, 20]", ctx) == false);
    assert(eval_bool_both("x not within [16, 20]", ctx) == true);
    assert(eval_bool_both("x within [min=10, max=20]", ctx) == true);
    assert(eval_bool_both("x within [max=20, min=10]", ctx) == true);

    /* Boundary: inclusive by default. */
    cxpr_context_set(ctx, "x", 20.0);
    assert(eval_bool_both("x within [10, 20]", ctx) == true);

    cxpr_context_free(ctx);
    printf("  within interval OK\n");
}

static void test_errors(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    /* Empty set is rejected. */
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "x in []", &err);
    assert(ast == NULL && err.code == CXPR_ERR_SYNTAX);

    /* Unclosed set bracket. */
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "x in [1, 2", &err);
    assert(ast == NULL && err.code == CXPR_ERR_SYNTAX);

    /* within still needs a bracketed interval. */
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "x within (1, 2)", &err);
    assert(ast == NULL && err.code == CXPR_ERR_SYNTAX);

    cxpr_parser_free(p);
    printf("  membership errors OK\n");
}

int main(void) {
    printf("membership tests:\n");
    test_numeric_set_membership();
    test_string_set_membership();
    test_within_interval();
    test_errors();
    printf("All membership tests passed.\n");
    return 0;
}
