/**
 * @file ir.test.c
 * @brief Unit tests for the internal cxpr IR compiler/evaluator.
 *
 * Step 5-6 coverage:
 * - numeric literal compilation
 * - PUSH_CONST / RETURN evaluation
 * - AST-eval parity for constant expressions
 * - identifier compilation
 * - LOAD_VAR evaluation and unknown identifier errors
 * - parameter compilation
 * - LOAD_PARAM evaluation and unknown parameter errors
 * - binary + and -
 * - unary -
 * - binary * and /
 * - division by zero semantics
 * - field access
 * - comparison operators
 * - logical operators and short-circuit
 * - ternary
 * - function calls via AST fallback
 * - low-risk constant folding
 */

#include "internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static double fn_distance3(const double* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    const double dx = args[0] - args[3];
    const double dy = args[1] - args[4];
    const double dz = args[2] - args[5];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static void test_ir_compile_number_literal(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "42", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 2);
    assert(program.code[0].op == CXPR_OP_PUSH_CONST);
    ASSERT_DOUBLE_EQ(program.code[0].value, 42.0);
    assert(program.code[1].op == CXPR_OP_RETURN);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_number_literal\n");
}

static void test_ir_eval_number_literal_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "3.14", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_number_literal_matches_ast\n");
}

static void test_ir_compile_identifier(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "price", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 2);
    assert(program.code[0].op == CXPR_OP_LOAD_VAR);
    assert(strcmp(program.code[0].name, "price") == 0);
    assert(program.code[1].op == CXPR_OP_RETURN);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_identifier\n");
}

static void test_ir_eval_identifier_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "price", &err);
    assert(ast);
    cxpr_context_set(ctx, "price", 123.5);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_identifier_matches_ast\n");
}

static void test_ir_eval_unknown_identifier(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "missing_value", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_eval(&program, ctx, NULL, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Unknown identifier") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_unknown_identifier\n");
}

static void test_ir_compile_parameter(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "$threshold", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 2);
    assert(program.code[0].op == CXPR_OP_LOAD_PARAM);
    assert(strcmp(program.code[0].name, "threshold") == 0);
    assert(program.code[1].op == CXPR_OP_RETURN);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_parameter\n");
}

static void test_ir_eval_parameter_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "$threshold", &err);
    assert(ast);
    cxpr_context_set_param(ctx, "threshold", 77.25);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_parameter_matches_ast\n");
}

static void test_ir_eval_unknown_parameter(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "$missing_threshold", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_eval(&program, ctx, NULL, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Unknown parameter variable") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_unknown_parameter\n");
}

static void test_ir_eval_addition_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "2 + 3", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 5.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_addition_matches_ast\n");
}

static void test_ir_eval_nested_add_sub_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b - $c", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 10.0);
    cxpr_context_set(ctx, "b", 4.5);
    cxpr_context_set_param(ctx, "c", 3.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 11.5);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_nested_add_sub_matches_ast\n");
}

static void test_ir_eval_unary_minus_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "-price", &err);
    assert(ast);
    cxpr_context_set(ctx, "price", 7.25);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, -7.25);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_unary_minus_matches_ast\n");
}

static void test_ir_eval_multiplication_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "6 * 7", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 42.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_multiplication_matches_ast\n");
}

static void test_ir_eval_division_and_precedence_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b * c / $d", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 2.0);
    cxpr_context_set(ctx, "b", 9.0);
    cxpr_context_set(ctx, "c", 4.0);
    cxpr_context_set_param(ctx, "d", 6.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 8.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_division_and_precedence_matches_ast\n");
}

static void test_ir_eval_division_by_zero(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "10 / denominator", &err);
    assert(ast);
    cxpr_context_set(ctx, "denominator", 0.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_eval(&program, ctx, NULL, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_DIVISION_BY_ZERO);
    assert(strcmp(err.message, "Division by zero") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_division_by_zero\n");
}

static void test_ir_eval_field_access_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "body.vx", &err);
    assert(ast);
    cxpr_context_set(ctx, "body.vx", 12.5);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.code[0].op == CXPR_OP_LOAD_FIELD);
    assert(strcmp(program.code[0].name, "body.vx") == 0);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 12.5);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_field_access_matches_ast\n");
}

static void test_ir_eval_unknown_field_access(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "body.vy", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_eval(&program, ctx, NULL, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Unknown field access") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_unknown_field_access\n");
}

static void test_ir_eval_comparisons_match_ast(void) {
    const char* exprs[] = {
        "a == b",
        "a != b",
        "a < b",
        "a <= b",
        "a > b",
        "a >= b",
    };

    for (size_t i = 0; i < sizeof(exprs) / sizeof(exprs[0]); i++) {
        cxpr_parser* p = cxpr_parser_new();
        cxpr_context* ctx = cxpr_context_new();
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_error err = {0};
        cxpr_ast* ast = cxpr_parse(p, exprs[i], &err);
        assert(ast);
        cxpr_context_set(ctx, "a", 5.0);
        cxpr_context_set(ctx, "b", 7.0);

        cxpr_ir_program program = {0};
        assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
        assert(err.code == CXPR_OK);

        double ast_result = cxpr_eval(ast, ctx, reg, &err);
        assert(err.code == CXPR_OK);
        double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
        assert(err.code == CXPR_OK);

        ASSERT_DOUBLE_EQ(ast_result, ir_result);

        cxpr_ir_program_reset(&program);
        cxpr_ast_free(ast);
        cxpr_registry_free(reg);
        cxpr_context_free(ctx);
        cxpr_parser_free(p);
    }

    printf("  ✓ test_ir_eval_comparisons_match_ast\n");
}

static void test_ir_eval_not_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "not price", &err);
    assert(ast);
    cxpr_context_set(ctx, "price", 0.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 1.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_not_matches_ast\n");
}

static void test_ir_eval_and_short_circuit_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a and missing_rhs", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 0.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 0.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_and_short_circuit_matches_ast\n");
}

static void test_ir_eval_or_short_circuit_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a or missing_rhs", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 5.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 1.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_or_short_circuit_matches_ast\n");
}

static void test_ir_eval_ternary_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "flag ? 10 : 20", &err);
    assert(ast);
    cxpr_context_set(ctx, "flag", 1.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 10.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_ternary_matches_ast\n");
}

static void test_ir_eval_nested_ternary_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "flag ? (a + 1) : (b - 2)", &err);
    assert(ast);
    cxpr_context_set(ctx, "flag", 0.0);
    cxpr_context_set(ctx, "a", 3.0);
    cxpr_context_set(ctx, "b", 8.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 6.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_nested_ternary_matches_ast\n");
}

static void test_ir_eval_builtin_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_register_builtins(reg);
    cxpr_ast* ast = cxpr_parse(p, "sqrt(9) + abs(-2)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 5.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_builtin_function_matches_ast\n");
}

static void test_ir_eval_struct_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y", "z"};
    cxpr_registry_add_struct_fn(reg, "distance3", fn_distance3, fields, 3, 2, NULL, NULL);
    cxpr_ast* ast = cxpr_parse(p, "distance3(goal, pose)", &err);
    assert(ast);
    cxpr_context_set(ctx, "goal.x", 3.0);
    cxpr_context_set(ctx, "goal.y", 4.0);
    cxpr_context_set(ctx, "goal.z", 0.0);
    cxpr_context_set(ctx, "pose.x", 0.0);
    cxpr_context_set(ctx, "pose.y", 0.0);
    cxpr_context_set(ctx, "pose.z", 0.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 5.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_struct_function_matches_ast\n");
}

static void test_ir_eval_defined_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_register_builtins(reg);
    err = cxpr_registry_define(reg, "sum2(a, b) => a + b");
    assert(err.code == CXPR_OK);
    cxpr_ast* ast = cxpr_parse(p, "sum2(x, y)", &err);
    assert(ast);
    cxpr_context_set(ctx, "x", 4.0);
    cxpr_context_set(ctx, "y", 6.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 10.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_defined_function_matches_ast\n");
}

static void test_ir_eval_nested_defined_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_register_builtins(reg);
    err = cxpr_registry_define(reg, "sq(x) => x * x");
    assert(err.code == CXPR_OK);
    err = cxpr_registry_define(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))");
    assert(err.code == CXPR_OK);
    cxpr_ast* ast = cxpr_parse(p, "hyp2(x, y)", &err);
    assert(ast);
    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 5.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_nested_defined_function_matches_ast\n");
}

static void test_ir_compile_repeated_multiplication_to_square(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "x * x + y * y", &err);
    assert(ast);
    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 4);
    assert(program.code[0].op == CXPR_OP_LOAD_VAR_SQUARE);
    assert(program.code[1].op == CXPR_OP_LOAD_VAR_SQUARE);
    assert(program.code[2].op == CXPR_OP_ADD);
    assert(program.code[3].op == CXPR_OP_RETURN);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, 25.0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_repeated_multiplication_to_square\n");
}

static void test_ir_constant_folding_reduces_program(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "2 + 3 * 4", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 2);
    assert(program.code[0].op == CXPR_OP_PUSH_CONST);
    ASSERT_DOUBLE_EQ(program.code[0].value, 14.0);
    assert(program.code[1].op == CXPR_OP_RETURN);

    double ast_result = cxpr_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(ast_result, ir_result);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_constant_folding_reduces_program\n");
}

static void test_ir_constant_folding_keeps_div_zero_runtime_error(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "10 / 0", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, NULL, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count > 2);

    double result = cxpr_ir_eval(&program, ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_DIVISION_BY_ZERO);
    assert(strcmp(err.message, "Division by zero") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_constant_folding_keeps_div_zero_runtime_error\n");
}

int main(void) {
    printf("Running IR tests...\n");
    test_ir_compile_number_literal();
    test_ir_eval_number_literal_matches_ast();
    test_ir_compile_identifier();
    test_ir_eval_identifier_matches_ast();
    test_ir_eval_unknown_identifier();
    test_ir_compile_parameter();
    test_ir_eval_parameter_matches_ast();
    test_ir_eval_unknown_parameter();
    test_ir_eval_addition_matches_ast();
    test_ir_eval_nested_add_sub_matches_ast();
    test_ir_eval_unary_minus_matches_ast();
    test_ir_eval_multiplication_matches_ast();
    test_ir_eval_division_and_precedence_matches_ast();
    test_ir_eval_division_by_zero();
    test_ir_eval_field_access_matches_ast();
    test_ir_eval_unknown_field_access();
    test_ir_eval_comparisons_match_ast();
    test_ir_eval_not_matches_ast();
    test_ir_eval_and_short_circuit_matches_ast();
    test_ir_eval_or_short_circuit_matches_ast();
    test_ir_eval_ternary_matches_ast();
    test_ir_eval_nested_ternary_matches_ast();
    test_ir_eval_builtin_function_matches_ast();
    test_ir_eval_struct_function_matches_ast();
    test_ir_eval_defined_function_matches_ast();
    test_ir_eval_nested_defined_function_matches_ast();
    test_ir_compile_repeated_multiplication_to_square();
    test_ir_constant_folding_reduces_program();
    test_ir_constant_folding_keeps_div_zero_runtime_error();
    printf("All IR tests passed!\n");
    return 0;
}
