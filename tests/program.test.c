/**
 * @file program.test.c
 * @brief Public API tests for compiled cxpr programs.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cxpr_test_internal.h"

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

    double result = cxpr_test_eval_program_number(prog, ctx, reg, &err);
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

    bool result = cxpr_test_eval_program_bool(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_bool\n");
}

static void test_program_eval_bool_param(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "$enabled and x > y", &err);
    cxpr_program* prog;
    bool result;

    assert(ast);
    cxpr_context_set_param_bool(ctx, "enabled", true);
    cxpr_context_set(ctx, "x", 2.0);
    cxpr_context_set(ctx, "y", 1.0);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    result = cxpr_test_eval_program_bool(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true);

    cxpr_context_set_param_bool(ctx, "enabled", false);
    result = cxpr_test_eval_program_bool(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == false);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_bool_param\n");
}

static void test_program_eval_root_bool_param(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "$enabled", &err);
    cxpr_program* prog;
    bool result;

    assert(ast);
    cxpr_context_set_param_bool(ctx, "enabled", true);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    result = cxpr_test_eval_program_bool(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result == true);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_root_bool_param\n");
}

static void test_program_eval_out_api(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b * 2", &err);
    cxpr_program* prog;
    double result = 0.0;

    assert(ast);
    cxpr_context_set(ctx, "a", 3.0);
    cxpr_context_set(ctx, "b", 4.0);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(cxpr_eval_program_number(prog, ctx, reg, &result, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(result, 11.0);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_out_api\n");
}

static void test_program_eval_number_rejects_bool_result(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "price > $limit", &err);
    cxpr_program* prog;
    double result = 0.0;

    assert(ast);
    cxpr_context_set(ctx, "price", 10.0);
    cxpr_context_set_param(ctx, "limit", 5.0);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    assert(!cxpr_eval_program_number(prog, ctx, reg, &result, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_number_rejects_bool_result\n");
}

static void test_program_if_requires_bool_condition(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast_bool;
    cxpr_ast* ast_number;
    cxpr_program* prog_bool;
    cxpr_program* prog_number;
    double result = 0.0;

    assert(p);
    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);

    ast_bool = cxpr_parse(p, "if(true, 10, 20)", &err);
    assert(ast_bool);
    prog_bool = cxpr_compile(ast_bool, reg, &err);
    assert(prog_bool);
    assert(cxpr_eval_program_number(prog_bool, ctx, reg, &result, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(result, 10.0);

    ast_number = cxpr_parse(p, "if(1.0, 30, 40)", &err);
    assert(ast_number);
    prog_number = cxpr_compile(ast_number, reg, &err);
    assert(prog_number);
    assert(!cxpr_eval_program_number(prog_number, ctx, reg, &result, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_program_free(prog_bool);
    cxpr_program_free(prog_number);
    cxpr_ast_free(ast_bool);
    cxpr_ast_free(ast_number);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_if_requires_bool_condition\n");
}

static void test_program_eval_double_rejects_bool_intermediate_arithmetic(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "1 + (price > $limit)", &err);
    cxpr_program* prog;
    double result;

    assert(ast);
    cxpr_context_set(ctx, "price", 10.0);
    cxpr_context_set_param(ctx, "limit", 5.0);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    result = cxpr_test_eval_program_number(prog, ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_eval_double_rejects_bool_intermediate_arithmetic\n");
}

static void test_program_dump(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b * 2", &err);
    assert(ast);

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    FILE* tmp = tmpfile();
    assert(tmp);
    cxpr_program_dump(prog, tmp);
    fflush(tmp);
    rewind(tmp);

    char buf[256];
    size_t n = fread(buf, 1, sizeof(buf) - 1, tmp);
    buf[n] = '\0';
    assert(strstr(buf, "LOAD_VAR") != NULL);
    assert(strstr(buf, "RETURN") != NULL);

    fclose(tmp);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_program_dump\n");
}

int main(void) {
    printf("Running program tests...\n");
    test_program_compile_and_eval();
    test_program_eval_bool();
    test_program_eval_bool_param();
    test_program_eval_root_bool_param();
    test_program_eval_out_api();
    test_program_eval_number_rejects_bool_result();
    test_program_if_requires_bool_condition();
    test_program_eval_double_rejects_bool_intermediate_arithmetic();
    test_program_dump();
    printf("All program tests passed!\n");
    return 0;
}
