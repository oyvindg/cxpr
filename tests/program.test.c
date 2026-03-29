/**
 * @file program.test.c
 * @brief Public API tests for compiled cxpr programs.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static void test_program_compile_and_eval(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b * 2", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 3.0);
    cxpr_context_set(ctx, "b", 4.0);

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    double result = cxpr_program_eval(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(result, 11.0);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_compile_and_eval\n");
}

static void test_program_eval_bool(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "price > $limit", &err);
    assert(ast);
    cxpr_context_set(ctx, "price", 10.0);
    cxpr_context_set_param(ctx, "limit", 5.0);

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    bool result = cxpr_program_eval_bool(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_bool\n");
}

int main(void) {
    printf("Running program tests...\n");
    test_program_compile_and_eval();
    test_program_eval_bool();
    printf("All program tests passed!\n");
    return 0;
}
