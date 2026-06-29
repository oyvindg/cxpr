/**
 * @file membership.test.c
 * @brief Tests for `in` (set membership) and `within` (interval membership).
 *
 * `in [a, b, c]` desugars to `contains(x, [a, b, c])`, so it works for
 * scalar types that equality supports. `within(x, lo, hi)` is a builtin
 * interval predicate. Both are checked through the tree-walk and compiled paths.
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
    if (tree_r != prog_r) {
        fprintf(stderr, "membership mismatch for `%s`: tree=%d compiled=%d\n",
                expr, (int)tree_r, (int)prog_r);
        fprintf(stderr, "result kind=%d\n", (int)cxpr_ir_view_program_result_kind(prog));
        cxpr_program_dump(prog, stderr);
        assert(tree_r == prog_r); /* engines must agree */
    }

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    return tree_r;
}

static void test_numeric_set_membership(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_set(ctx, "x", 20.0);
    cxpr_context_set_param(ctx, "a", 10.0);
    cxpr_context_set_param(ctx, "b", 20.0);

    assert(eval_bool_both("x in [10, 20, 30]", ctx) == true);
    assert(eval_bool_both("x in [10, 30]", ctx) == false);
    assert(eval_bool_both("x not in [10, 30]", ctx) == true);
    assert(eval_bool_both("x not in [10, 20, 30]", ctx) == false);
    assert(eval_bool_both("x in [20]", ctx) == true); /* single element */
    assert(eval_bool_both("x in [$a, $b]", ctx) == true);

    cxpr_context_free(ctx);
    printf("  numeric set membership OK\n");
}

static void test_string_set_membership(void) {
    /* Enum-like string membership. */
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

    assert(eval_bool_both("within(x, 10, 20)", ctx) == true);
    assert(eval_bool_both("within(x, 16, 20)", ctx) == false);
    assert(eval_bool_both("not within(x, 16, 20)", ctx) == true);
    assert(eval_bool_both("within(source=x, min=10, max=20)", ctx) == true);
    assert(eval_bool_both("within(max=20, source=x, min=10)", ctx) == true);

    /* Boundary: inclusive by default. */
    cxpr_context_set(ctx, "x", 20.0);
    assert(eval_bool_both("within(x, 10, 20)", ctx) == true);
    assert(eval_bool_both("within(x, 10, 20, include_max=false)", ctx) == false);
    assert(eval_bool_both("within(source=x, min=10, max=20, include_min=true, include_max=true)", ctx) == true);

    cxpr_context_set(ctx, "x", 10.0);
    assert(eval_bool_both("within(x, 10, 20, false, true)", ctx) == false);
    assert(eval_bool_both("within(x, 10, 20, true, true)", ctx) == true);

    cxpr_context_free(ctx);
    printf("  within interval OK\n");
}

static void test_contains_array_membership(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_value numeric_values[] = { cxpr_num(10.0), cxpr_num(20.0), cxpr_num(30.0) };
    cxpr_value string_values[] = { cxpr_string("US"), cxpr_string("EU"), cxpr_string("APAC") };
    cxpr_value allowed = cxpr_array(cxpr_array_value_new(numeric_values, 3u));
    cxpr_value regions = cxpr_array(cxpr_array_value_new(string_values, 3u));

    assert(allowed.a != NULL);
    assert(regions.a != NULL);
    cxpr_context_set(ctx, "x", 20.0);
    cxpr_context_set_string(ctx, "region", "EU");
    cxpr_context_set_value(ctx, "allowed", &allowed);
    cxpr_context_set_param_value(ctx, "regions", &regions);

    assert(eval_bool_both("contains(x, allowed)", ctx) == true);
    assert(eval_bool_both("contains(x, [10, 20, 30])", ctx) == true);
    assert(eval_bool_both("contains(15, allowed)", ctx) == false);
    assert(eval_bool_both("contains(source=region, values=$regions)", ctx) == true);
    assert(eval_bool_both("not contains(value=\"LATAM\", array=$regions)", ctx) == true);

    cxpr_value_free(&allowed);
    cxpr_value_free(&regions);
    cxpr_context_free(ctx);
    printf("  contains array membership OK\n");
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

    /* The old infix interval sugar was removed; use within(x, lo, hi). */
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "x within [1, 2]", &err);
    assert(ast == NULL && err.code == CXPR_ERR_SYNTAX);

    cxpr_parser_free(p);
    printf("  membership errors OK\n");
}

int main(void) {
    printf("membership tests:\n");
    test_numeric_set_membership();
    test_string_set_membership();
    test_within_interval();
    test_contains_array_membership();
    test_errors();
    printf("All membership tests passed.\n");
    return 0;
}
