/**
 * @file codegen.test.c
 * @brief Tests for cxpr_ast_to_c / cxpr_exprset_to_c.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* to_c(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    assert(ast && err.code == CXPR_OK);
    char* out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out && err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return out;
}

static void eq(const char* expr, const char* want) {
    char* got = to_c(expr);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "to_c(\"%s\") = \"%s\", want \"%s\"\n", expr, got, want);
        assert(0);
    }
    free(got);
}

static void test_operators(void) {
    eq("a + b", "(a + b)");
    eq("a * b + c", "((a * b) + c)");
    eq("a^2", "pow(a, 2)");            /* power -> pow */
    eq("c ^ 2", "pow(c, 2)");
    eq("a % b", "fmod(a, b)");          /* % -> fmod */
    eq("a and b or c", "((a && b) || c)");
    eq("not a", "(!a)");
    eq("-x", "(-x)");
    eq("a < b", "(a < b)");
    eq("x ? a : b", "(x ? a : b)");
    eq("$thr", "thr");                  /* $param -> bare name */
    printf("  operators OK\n");
}

static void test_functions(void) {
    eq("sqrt(x)", "sqrt(x)");
    eq("abs(x)", "fabs(x)");            /* abs -> fabs */
    eq("hypot(a, b)", "hypot(a, b)");
    eq("min(a, b)", "fmin(a, b)");      /* variadic min -> nested fmin */
    eq("min(a, b, c)", "fmin(fmin(a, b), c)");
    eq("max(a, b, c)", "fmax(fmax(a, b), c)");
    printf("  functions OK\n");
}

static void test_membership_desugar(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "s in [1, 2]", &err);
    char* printed;
    char* out;

    assert(ast && err.code == CXPR_OK);
    printed = cxpr_ast_to_string(ast);
    assert(printed && strcmp(printed, "contains(s, [1, 2])") == 0);
    free(printed);

    err = (cxpr_error){0};
    out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL && err.code != CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  membership desugar OK\n");
}

static void test_unsupported(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};

    /* unknown function with no mapping -> error */
    cxpr_ast* ast = cxpr_parse(p, "mystery(x)", &err);
    assert(ast);
    err = (cxpr_error){0};
    char* out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL && err.code != CXPR_OK);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
    printf("  unsupported rejected OK\n");
}

static void test_exprset_topo(void) {
    /* Interdependent set, declared out of order. Must emit in dependency
     * order: r_s before f before dr_dl. */
    const char* names[] = { "dr_dl", "f", "r_s" };
    const char* srcs[]  = { "p_r * f", "1 - r_s / r", "2 * G * M / c^2" };

    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_c_named_expr defs[3];
    cxpr_ast* asts[3];
    for (int i = 0; i < 3; ++i) {
        asts[i] = cxpr_parse(p, srcs[i], &err);
        assert(asts[i] && err.code == CXPR_OK);
        defs[i].name = names[i];
        defs[i].ast = asts[i];
    }

    char* block = cxpr_exprset_to_c(defs, 3, "double", NULL, &err);
    assert(block && err.code == CXPR_OK);

    /* r_s defined before f, f before dr_dl. */
    char* p_rs = strstr(block, "double r_s");
    char* p_f  = strstr(block, "double f ");
    char* p_dr = strstr(block, "double dr_dl");
    assert(p_rs && p_f && p_dr);
    assert(p_rs < p_f && p_f < p_dr);
    /* power transpiled inside the block too */
    assert(strstr(block, "pow(c, 2)"));

    free(block);
    for (int i = 0; i < 3; ++i) cxpr_ast_free(asts[i]);
    cxpr_parser_free(p);
    printf("  exprset topo-order OK\n");
}

static void test_exprset_cycle(void) {
    const char* names[] = { "a", "b" };
    const char* srcs[]  = { "b + 1", "a + 1" }; /* a<->b cycle */
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_c_named_expr defs[2];
    cxpr_ast* asts[2];
    for (int i = 0; i < 2; ++i) { asts[i] = cxpr_parse(p, srcs[i], &err); assert(asts[i]); defs[i].name = names[i]; defs[i].ast = asts[i]; }

    err = (cxpr_error){0};
    char* block = cxpr_exprset_to_c(defs, 2, "double", NULL, &err);
    assert(block == NULL && err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    for (int i = 0; i < 2; ++i) cxpr_ast_free(asts[i]);
    cxpr_parser_free(p);
    printf("  exprset cycle detected OK\n");
}

int main(void) {
    printf("codegen tests:\n");
    test_operators();
    test_functions();
    test_membership_desugar();
    test_unsupported();
    test_exprset_topo();
    test_exprset_cycle();
    printf("All codegen tests passed.\n");
    return 0;
}
