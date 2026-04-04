/**
 * @file ir.test.c
 * @brief Unit tests for the internal cxpr IR compiler/evaluator.
 *
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
 * - native and defined function calls
 * - fast-result-kind metadata for scalar vs non-scalar fast-paths
 * - low-risk constant folding
 */

#include "cxpr_test_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)
#define TEST_IR_RESULT_UNKNOWN 0
#define TEST_IR_RESULT_DOUBLE 1

static double fn_distance3(const double* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    const double dx = args[0] - args[3];
    const double dy = args[1] - args[4];
    const double dz = args[2] - args[5];
    return sqrt(dx * dx + dy * dy + dz * dz);
}

static double native_sq_test(double x) {
    return x * x;
}

static double native_hyp2_test(double x, double y) {
    return sqrt(x * x + y * y);
}

static double native_mix3_test(double x, double y, double z) {
    return x + y * 10.0 + z * 100.0;
}

static void producer_shift_x(const double* args, size_t argc, cxpr_value* out,
                             size_t field_count, void* userdata) {
    (void)argc;
    (void)field_count;
    (void)userdata;
    out[0] = cxpr_fv_double(args[0] + 1.0);
}

static void test_ir_eval_modulo_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a % b", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 17.5);
    cxpr_context_set(ctx, "b", 3.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, fmod(17.5, 3.0));

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_modulo_matches_ast\n");
}

static void test_ir_eval_modulo_by_zero(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "10 % 0", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_exec(&program, ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_DIVISION_BY_ZERO);
    assert(strcmp(err.message, "Modulo by zero") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_modulo_by_zero\n");
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double result = cxpr_ir_exec(&program, ctx, NULL, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double result = cxpr_ir_exec(&program, ctx, NULL, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double result = cxpr_ir_exec(&program, ctx, NULL, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double result = cxpr_ir_exec(&program, ctx, NULL, &err);
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
        cxpr_program* compiled = NULL;
        assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
        assert(err.code == CXPR_OK);
        compiled = cxpr_compile(ast, reg, &err);
        assert(compiled);
        assert(err.code == CXPR_OK);

        bool ast_result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
        assert(err.code == CXPR_OK);
        bool ir_result = cxpr_test_eval_program_bool(compiled, ctx, reg, &err);
        assert(err.code == CXPR_OK);

        assert(ast_result == ir_result);

        cxpr_program_free(compiled);
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
    cxpr_ast* ast = cxpr_parse(p, "not (price == 0)", &err);
    assert(ast);
    cxpr_context_set(ctx, "price", 1.0);

    cxpr_ir_program program = {0};
    cxpr_program* compiled = NULL;
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    compiled = cxpr_compile(ast, reg, &err);
    assert(compiled);
    assert(err.code == CXPR_OK);

    bool ast_result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    bool ir_result = cxpr_test_eval_program_bool(compiled, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    assert(ast_result == ir_result);

    cxpr_program_free(compiled);
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
    cxpr_ast* ast = cxpr_parse(p, "a == 1 and missing_rhs", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 0.0);

    cxpr_ir_program program = {0};
    cxpr_program* compiled = NULL;
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    compiled = cxpr_compile(ast, reg, &err);
    assert(compiled);
    assert(err.code == CXPR_OK);

    bool ast_result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    bool ir_result = cxpr_test_eval_program_bool(compiled, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    assert(ast_result == ir_result);
    assert(ir_result == false);

    cxpr_program_free(compiled);
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
    cxpr_ast* ast = cxpr_parse(p, "a == 5 or missing_rhs", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 5.0);

    cxpr_ir_program program = {0};
    cxpr_program* compiled = NULL;
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    compiled = cxpr_compile(ast, reg, &err);
    assert(compiled);
    assert(err.code == CXPR_OK);

    bool ast_result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    bool ir_result = cxpr_test_eval_program_bool(compiled, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    assert(ast_result == ir_result);
    assert(ir_result == true);

    cxpr_program_free(compiled);
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
    cxpr_ast* ast = cxpr_parse(p, "flag == 1 ? 10 : 20", &err);
    assert(ast);
    cxpr_context_set(ctx, "flag", 1.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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
    cxpr_ast* ast = cxpr_parse(p, "flag == 1 ? (a + 1) : (b - 2)", &err);
    assert(ast);
    cxpr_context_set(ctx, "flag", 0.0);
    cxpr_context_set(ctx, "a", 3.0);
    cxpr_context_set(ctx, "b", 8.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

static void test_ir_eval_intrinsics_match_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_register_builtins(reg);
    cxpr_context_set(ctx, "x", -2.4);
    cxpr_context_set(ctx, "y", 4.2);
    cxpr_context_set(ctx, "z", 0.7);
    cxpr_ast* ast = cxpr_parse(p, "round(x) + floor(y) + ceil(z) + sign(x) + clamp(y, 0, 3)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_intrinsics_match_ast\n");
}

static void test_ir_eval_struct_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y", "z"};
    cxpr_value goal_vals[] = {
        cxpr_fv_double(3.0),
        cxpr_fv_double(4.0),
        cxpr_fv_double(0.0),
    };
    cxpr_value pose_vals[] = {
        cxpr_fv_double(0.0),
        cxpr_fv_double(0.0),
        cxpr_fv_double(0.0),
    };
    cxpr_struct_value* goal = NULL;
    cxpr_struct_value* pose = NULL;
    cxpr_registry_add_fn(reg, "distance3", fn_distance3, fields, 3, 2, NULL, NULL);
    cxpr_ast* ast = cxpr_parse(p, "distance3(goal, pose)", &err);
    assert(ast);
    goal = cxpr_struct_value_new(fields, goal_vals, 3);
    pose = cxpr_struct_value_new(fields, pose_vals, 3);
    assert(goal);
    assert(pose);
    cxpr_context_set_struct(ctx, "goal", goal);
    cxpr_context_set_struct(ctx, "pose", pose);
    cxpr_struct_value_free(goal);
    cxpr_struct_value_free(pose);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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
    err = cxpr_registry_define_fn(reg, "sum2(a, b) => a + b");
    assert(err.code == CXPR_OK);
    cxpr_ast* ast = cxpr_parse(p, "sum2(x, y)", &err);
    assert(ast);
    cxpr_context_set(ctx, "x", 4.0);
    cxpr_context_set(ctx, "y", 6.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

static void test_ir_compile_unknown_function_fails(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "missing_fn(1)", &err);
    cxpr_ir_program program = {0};

    assert(ast);
    assert(cxpr_ir_compile(ast, reg, &program, &err) == false);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    assert(strcmp(err.message, "Unknown function") == 0);
    assert(program.code == NULL);
    assert(program.count == 0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_unknown_function_fails\n");
}

static void test_ir_compile_unknown_producer_fails(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "missing_prod(12, 26).line", &err);
    cxpr_ir_program program = {0};

    assert(ast);
    assert(cxpr_ir_compile(ast, reg, &program, &err) == false);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    assert(strcmp(err.message, "Unknown function") == 0);
    assert(program.code == NULL);
    assert(program.count == 0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_unknown_producer_fails\n");
}

static void test_ir_eval_nested_defined_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_register_builtins(reg);
    err = cxpr_registry_define_fn(reg, "sq(x) => x * x");
    assert(err.code == CXPR_OK);
    err = cxpr_registry_define_fn(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))");
    assert(err.code == CXPR_OK);
    cxpr_ast* ast = cxpr_parse(p, "hyp2(x, y)", &err);
    assert(ast);
    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

static void test_ir_eval_struct_param_defined_function_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double p_vals[] = {3.0, 4.0};
    double q_vals[] = {0.0, 0.0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    err = cxpr_registry_define_fn(reg,
                                  "dist2(p, q) => sqrt((p.x - q.x)*(p.x - q.x) + "
                                  "(p.y - q.y)*(p.y - q.y))");
    assert(err.code == CXPR_OK);

    cxpr_context_set_fields(ctx, "foo", fields, p_vals, 2);
    cxpr_context_set_fields(ctx, "bar", fields, q_vals, 2);

    ast = cxpr_parse(p, "dist2(foo, bar)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(cxpr_test_eval_ast_number(ast, ctx, reg, &err), 5.0);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 5.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_struct_param_defined_function_matches_ast\n");
}

static void test_ir_eval_struct_param_field_substitution(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double lhs_vals[] = {2.0, 7.0};
    double rhs_vals[] = {11.0, 5.0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    err = cxpr_registry_define_fn(reg, "pick(p, q) => p.x * 100 + q.y");
    assert(err.code == CXPR_OK);

    cxpr_context_set_fields(ctx, "left", fields, lhs_vals, 2);
    cxpr_context_set_fields(ctx, "right", fields, rhs_vals, 2);

    ast = cxpr_parse(p, "pick(left, right)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 205.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_struct_param_field_substitution\n");
}

static void test_ir_eval_struct_param_defined_function_nested_with_inline_limit(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double p_vals[] = {3.0, 4.0};
    double q_vals[] = {0.0, 0.0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    assert(cxpr_registry_define_fn(reg,
                                   "d0(a, b) => sqrt((a.x - b.x)*(a.x - b.x) + "
                                   "(a.y - b.y)*(a.y - b.y))")
               .code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d1(a, b) => d0(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d2(a, b) => d1(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d3(a, b) => d2(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d4(a, b) => d3(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d5(a, b) => d4(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d6(a, b) => d5(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d7(a, b) => d6(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d8(a, b) => d7(a, b)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "d9(a, b) => d8(a, b)").code == CXPR_OK);

    cxpr_context_set_fields(ctx, "p0", fields, p_vals, 2);
    cxpr_context_set_fields(ctx, "p1", fields, q_vals, 2);

    ast = cxpr_parse(p, "d9(p0, p1)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    (void)cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    (void)cxpr_ir_exec(&program, ctx, reg, &err);
    err.code = CXPR_OK;

    for (size_t i = 0; i < 8; ++i) {
        cxpr_ir_program_reset(&program);
        assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
        assert(err.code == CXPR_OK);
        (void)cxpr_ir_exec(&program, ctx, reg, &err);
        err.code = CXPR_OK;
    }

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_struct_param_defined_function_nested_with_inline_limit\n");
}

static void test_ir_compile_reset_struct_defined_function_repeatedly(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double left_vals[] = {3.0, 4.0};
    double right_vals[] = {0.0, 0.0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    assert(cxpr_registry_define_fn(reg,
                                   "dist2(p, q) => sqrt((p.x - q.x)*(p.x - q.x) + "
                                   "(p.y - q.y)*(p.y - q.y))")
               .code == CXPR_OK);
    cxpr_context_set_fields(ctx, "lhs", fields, left_vals, 2);
    cxpr_context_set_fields(ctx, "rhs", fields, right_vals, 2);

    ast = cxpr_parse(p, "dist2(lhs, rhs) + dist2(lhs, rhs)", &err);
    assert(ast);

    for (size_t i = 0; i < 256; ++i) {
        cxpr_ir_program program = {0};
        assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
        assert(err.code == CXPR_OK);
        ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 10.0);
        assert(err.code == CXPR_OK);
        cxpr_ir_program_reset(&program);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_reset_struct_defined_function_repeatedly\n");
}

static void test_ir_fast_result_kind_scalar_defined_function(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    assert(cxpr_registry_define_fn(reg, "twice(v) => v * 2").code == CXPR_OK);
    cxpr_context_set(ctx, "a", 3.0);

    ast = cxpr_parse(p, "twice(a) + 1", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.fast_result_kind == TEST_IR_RESULT_DOUBLE);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 7.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_fast_result_kind_scalar_defined_function\n");
}

static void test_ir_fast_result_kind_struct_defined_function_stays_unknown(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double left_vals[] = {3.0, 4.0};
    double right_vals[] = {0.0, 0.0};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    assert(cxpr_registry_define_fn(reg,
                                   "dist2(p, q) => sqrt((p.x - q.x)*(p.x - q.x) + "
                                   "(p.y - q.y)*(p.y - q.y))")
               .code == CXPR_OK);
    cxpr_context_set_fields(ctx, "lhs", fields, left_vals, 2);
    cxpr_context_set_fields(ctx, "rhs", fields, right_vals, 2);

    ast = cxpr_parse(p, "dist2(lhs, rhs)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.fast_result_kind == TEST_IR_RESULT_UNKNOWN);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 5.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_fast_result_kind_struct_defined_function_stays_unknown\n");
}

static void test_ir_fast_result_kind_producer_field_access_stays_unknown(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x"};
    cxpr_ast* ast;

    cxpr_register_builtins(reg);
    cxpr_registry_add_struct(reg, "shift", producer_shift_x, 1, 1, fields, 1, NULL, NULL);

    ast = cxpr_parse(p, "shift(3).x + 1", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.fast_result_kind == TEST_IR_RESULT_UNKNOWN);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err), 5.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_fast_result_kind_producer_field_access_stays_unknown\n");
}

static void test_ir_eval_native_function_fast_path_matches_ast(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_registry_add_unary(reg, "native_sq", native_sq_test);
    cxpr_registry_add_binary(reg, "native_hyp2", native_hyp2_test);
    cxpr_ast* ast = cxpr_parse(p, "native_hyp2(a, b) + native_hyp2(c, d) - native_sq(e)", &err);
    assert(ast);
    cxpr_context_set(ctx, "a", 1.5);
    cxpr_context_set(ctx, "b", 2.5);
    cxpr_context_set(ctx, "c", 3.5);
    cxpr_context_set(ctx, "d", 4.5);
    cxpr_context_set(ctx, "e", 5.5);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    ASSERT_DOUBLE_EQ(ast_result, ir_result);
    ASSERT_DOUBLE_EQ(ir_result, native_hyp2_test(1.5, 2.5) + native_hyp2_test(3.5, 4.5) - native_sq_test(5.5));

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_native_function_fast_path_matches_ast\n");
}

static void test_ir_compile_native_function_uses_specialized_call_ops(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_registry_add_unary(reg, "native_sq", native_sq_test);
    cxpr_registry_add_binary(reg, "native_hyp2", native_hyp2_test);
    cxpr_registry_add_ternary(reg, "native_mix3", native_mix3_test);
    cxpr_ast* ast =
        cxpr_parse(p, "native_sq(a) + native_hyp2(b, c) + native_mix3(d, e, f)", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    assert(program.count == 12);
    assert(program.code[0].op == CXPR_OP_LOAD_VAR);
    assert(program.code[1].op == CXPR_OP_CALL_UNARY);
    assert(program.code[2].op == CXPR_OP_LOAD_VAR);
    assert(program.code[3].op == CXPR_OP_LOAD_VAR);
    assert(program.code[4].op == CXPR_OP_CALL_BINARY);
    assert(program.code[5].op == CXPR_OP_ADD);
    assert(program.code[6].op == CXPR_OP_LOAD_VAR);
    assert(program.code[7].op == CXPR_OP_LOAD_VAR);
    assert(program.code[8].op == CXPR_OP_LOAD_VAR);
    assert(program.code[9].op == CXPR_OP_CALL_TERNARY);
    assert(program.code[10].op == CXPR_OP_ADD);
    assert(program.code[11].op == CXPR_OP_RETURN);
    cxpr_context_set(ctx, "a", 2.0);
    cxpr_context_set(ctx, "b", 3.0);
    cxpr_context_set(ctx, "c", 4.0);
    cxpr_context_set(ctx, "d", 5.0);
    cxpr_context_set(ctx, "e", 6.0);
    cxpr_context_set(ctx, "f", 7.0);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, ctx, reg, &err),
                     native_sq_test(2.0) + native_hyp2_test(3.0, 4.0) +
                         native_mix3_test(5.0, 6.0, 7.0));
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_native_function_uses_specialized_call_ops\n");
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

static void test_ir_eval_reuses_lookup_cache_across_value_updates(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "a + b * c - d / e + x * y - z", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    for (size_t i = 0; i < 16; ++i) {
        const double t = (double)i * 0.25;
        cxpr_context_set(ctx, "a", 1.5 + t);
        cxpr_context_set(ctx, "b", 2.5 + t * 2.0);
        cxpr_context_set(ctx, "c", 3.5 + t * 3.0);
        cxpr_context_set(ctx, "d", 4.5 + t * 4.0);
        cxpr_context_set(ctx, "e", 5.5 + t * 5.0);
        cxpr_context_set(ctx, "x", 11.5 - t);
        cxpr_context_set(ctx, "y", 12.5 + t * 0.5);
        cxpr_context_set(ctx, "z", 13.5 - t * 0.25);
        ASSERT_DOUBLE_EQ(cxpr_test_eval_ast_number(ast, ctx, reg, &err),
                         cxpr_ir_exec(&program, ctx, reg, &err));
        assert(err.code == CXPR_OK);
    }

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_reuses_lookup_cache_across_value_updates\n");
}

static void test_ir_eval_invalidates_parent_lookup_when_child_shadows(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* child = cxpr_context_overlay_new(parent);
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "x + 1", &err);
    cxpr_ir_program program = {0};

    assert(p);
    assert(parent);
    assert(child);
    assert(reg);
    assert(ast);

    cxpr_context_set(parent, "x", 2.0);
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, child, reg, &err), 3.0);
    assert(err.code == CXPR_OK);

    cxpr_context_set(child, "x", 7.0);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, child, reg, &err), 8.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(child);
    cxpr_context_free(parent);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_invalidates_parent_lookup_when_child_shadows\n");
}

static void test_ir_eval_invalidates_parent_lookup_when_owner_map_grows(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* child = cxpr_context_overlay_new(parent);
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "x + 1", &err);
    cxpr_ir_program program = {0};
    size_t fill_count = 64;
    char key[32];

    assert(p);
    assert(parent);
    assert(child);
    assert(reg);
    assert(ast);

    cxpr_context_set(parent, "x", 2.0);
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, child, reg, &err), 3.0);
    assert(err.code == CXPR_OK);

    for (size_t i = 0; i < fill_count; ++i) {
        snprintf(key, sizeof(key), "grow_%zu", i);
        cxpr_context_set(parent, key, (double)i);
    }
    cxpr_context_set(parent, "x", 7.0);
    ASSERT_DOUBLE_EQ(cxpr_ir_exec(&program, child, reg, &err), 8.0);
    assert(err.code == CXPR_OK);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(child);
    cxpr_context_free(parent);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_eval_invalidates_parent_lookup_when_owner_map_grows\n");
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

    double ast_result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    double ir_result = cxpr_ir_exec(&program, ctx, reg, &err);
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

    double result = cxpr_ir_exec(&program, ctx, reg, &err);
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

static void test_ir_exec_rejects_bool_result(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "1 < 2", &err);
    assert(ast);

    cxpr_ir_program program = {0};
    assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
    assert(err.code == CXPR_OK);

    double result = cxpr_ir_exec(&program, ctx, reg, &err);
    assert(isnan(result));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    assert(strcmp(err.message, "Expression did not evaluate to double") == 0);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_exec_rejects_bool_result\n");
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
    test_ir_eval_modulo_matches_ast();
    test_ir_eval_modulo_by_zero();
    test_ir_eval_field_access_matches_ast();
    test_ir_eval_unknown_field_access();
    test_ir_eval_comparisons_match_ast();
    test_ir_eval_not_matches_ast();
    test_ir_eval_and_short_circuit_matches_ast();
    test_ir_eval_or_short_circuit_matches_ast();
    test_ir_eval_ternary_matches_ast();
    test_ir_eval_nested_ternary_matches_ast();
    test_ir_eval_builtin_function_matches_ast();
    test_ir_eval_intrinsics_match_ast();
    test_ir_eval_struct_function_matches_ast();
    test_ir_eval_defined_function_matches_ast();
    test_ir_compile_unknown_function_fails();
    test_ir_compile_unknown_producer_fails();
    test_ir_eval_nested_defined_function_matches_ast();
    test_ir_eval_struct_param_defined_function_matches_ast();
    test_ir_eval_struct_param_field_substitution();
    test_ir_eval_struct_param_defined_function_nested_with_inline_limit();
    test_ir_compile_reset_struct_defined_function_repeatedly();
    test_ir_fast_result_kind_scalar_defined_function();
    test_ir_fast_result_kind_struct_defined_function_stays_unknown();
    test_ir_fast_result_kind_producer_field_access_stays_unknown();
    test_ir_eval_native_function_fast_path_matches_ast();
    test_ir_compile_native_function_uses_specialized_call_ops();
    test_ir_compile_repeated_multiplication_to_square();
    test_ir_eval_reuses_lookup_cache_across_value_updates();
    test_ir_eval_invalidates_parent_lookup_when_child_shadows();
    test_ir_eval_invalidates_parent_lookup_when_owner_map_grows();
    test_ir_constant_folding_reduces_program();
    test_ir_constant_folding_keeps_div_zero_runtime_error();
    test_ir_exec_rejects_bool_result();
    printf("All IR tests passed!\n");
    return 0;
}
