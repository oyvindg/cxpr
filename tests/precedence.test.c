/**
 * @file precedence.test.c
 * @brief Unit tests for operator precedence and associativity.
 *
 * Tests covered:
 * - Arithmetic precedence (* / % before + -)
 * - Power right-associativity (2^3^2 = 512)
 * - Comparison vs arithmetic (a + b < c + d)
 * - Logical precedence (and before or)
 * - Mixed precedence with parentheses
 * - Unary minus precedence (-a^2 vs (-a)^2)
 * - Equality vs relational split (a < b == c < d)
 * - Not vs comparison (not a > b)
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include "cxpr_test_internal.h"

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static double eval_expr(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    double result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return result;
}

static bool eval_bool_expr(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    bool result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return result;
}

static bool eval_bool_expr_ctx(const char* expr, const char* names[], const double values[]) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    for (int i = 0; names[i] != NULL; i++) {
        cxpr_context_set(ctx, names[i], values[i]);
    }

    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    bool result = cxpr_test_eval_ast_bool(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return result;
}

/**
 * @brief Evaluate expression with named context variables.
 * @param expr Expression string.
 * @param names NULL-terminated array of variable names.
 * @param values Array of values (same order as names).
 * @return Evaluated result.
 */
static double eval_expr_ctx(const char* expr, const char* names[], const double values[]) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    for (int i = 0; names[i] != NULL; i++) {
        cxpr_context_set(ctx, names[i], values[i]);
    }

    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }
    double result = cxpr_test_eval_ast_number(ast, ctx, reg, &err);
    if (err.code != CXPR_OK) {
        fprintf(stderr, "Eval failed: %s for '%s'\n", err.message, expr);
        assert(0);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Arithmetic precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_mul_before_add(void) {
    /* 2 + 3 * 4 = 2 + 12 = 14 */
    ASSERT_DOUBLE_EQ(eval_expr("2 + 3 * 4"), 14.0);
    /* 2 * 3 + 4 = 6 + 4 = 10 */
    ASSERT_DOUBLE_EQ(eval_expr("2 * 3 + 4"), 10.0);
    printf("  ✓ test_mul_before_add\n");
}

static void test_div_before_sub(void) {
    /* 10 - 6 / 2 = 10 - 3 = 7 */
    ASSERT_DOUBLE_EQ(eval_expr("10 - 6 / 2"), 7.0);
    printf("  ✓ test_div_before_sub\n");
}

static void test_modulo_same_as_mul(void) {
    /* 10 + 7 % 3 = 10 + 1 = 11 */
    ASSERT_DOUBLE_EQ(eval_expr("10 + 7 % 3"), 11.0);
    printf("  ✓ test_modulo_same_as_mul\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Power precedence and associativity
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_power_right_associative(void) {
    /* 2^3^2 = 2^(3^2) = 2^9 = 512 */
    ASSERT_DOUBLE_EQ(eval_expr("2 ^ 3 ^ 2"), 512.0);
    /* NOT (2^3)^2 = 8^2 = 64 */
    printf("  ✓ test_power_right_associative\n");
}

static void test_power_before_mul(void) {
    /* 2 * 3 ^ 2 = 2 * 9 = 18 */
    ASSERT_DOUBLE_EQ(eval_expr("2 * 3 ^ 2"), 18.0);
    printf("  ✓ test_power_before_mul\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Comparison vs arithmetic
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_comparison_after_arithmetic(void) {
    /* 2 + 3 < 10 should be (2+3) < 10 = true */
    assert(eval_bool_expr("2 + 3 < 10") == true);
    /* 2 + 3 > 10 should be (2+3) > 10 = false */
    assert(eval_bool_expr("2 + 3 > 10") == false);
    printf("  ✓ test_comparison_after_arithmetic\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Logical precedence (and before or)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_and_before_or(void) {
    /* false or true and true = false or (true and true) = false or true = true */
    assert(eval_bool_expr("false or true and true") == true);
    /* true or false and false = true or (false and false) = true or false = true */
    assert(eval_bool_expr("true or false and false") == true);
    printf("  ✓ test_and_before_or\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Not precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_not_precedence(void) {
    /* not true and false = (not true) and false = false and false = false */
    assert(eval_bool_expr("not true and false") == false);
    /* not false or true = (not false) or true = true or true = true */
    assert(eval_bool_expr("not false or true") == true);
    printf("  ✓ test_not_precedence\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Unary minus precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_unary_minus_precedence(void) {
    /* Power binds tighter than unary minus (standard math convention):
       Grammar: unary → ("-"|"+") unary | power
                power → primary [ ("^"|"**") power ]
       So -x^n is always parsed as -(x^n), never (-x)^n */

    /* -2^2 = -(2^2) = -4 */
    ASSERT_DOUBLE_EQ(eval_expr("-2 ^ 2"), -4.0);
    /* -3^2 = -(3^2) = -9 */
    ASSERT_DOUBLE_EQ(eval_expr("-3 ^ 2"), -9.0);
    /* (-2)^2 = 4 (explicit parentheses override) */
    ASSERT_DOUBLE_EQ(eval_expr("(-2) ^ 2"), 4.0);
    /* (-3)^2 = 9 */
    ASSERT_DOUBLE_EQ(eval_expr("(-3) ^ 2"), 9.0);
    /* -2^3 = -(2^3) = -8 */
    ASSERT_DOUBLE_EQ(eval_expr("-2 ^ 3"), -8.0);
    /* Chained: -2^2^3 = -(2^(2^3)) = -(2^8) = -256 */
    ASSERT_DOUBLE_EQ(eval_expr("-2 ^ 2 ^ 3"), -256.0);
    printf("  ✓ test_unary_minus_precedence\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Equality vs relational precedence (C/C++ convention)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_relational_before_equality(void) {
    /* a < b == c < d should parse as (a < b) == (c < d)
       With values: 1 < 5 == 3 < 10 → true == true → true */
    assert(eval_bool_expr_ctx("a < b == c < d", (const char*[]){"a","b","c","d",NULL}, (double[]){1,5,3,10}) == true);

    /* 5 < 1 == 10 < 3 → false == false → true */
    assert(eval_bool_expr_ctx("a < b == c < d", (const char*[]){"a","b","c","d",NULL}, (double[]){5,1,10,3}) == true);

    /* 1 < 5 != 10 < 3 → true != false → true */
    assert(eval_bool_expr_ctx("a < b != c < d", (const char*[]){"a","b","c","d",NULL}, (double[]){1,5,10,3}) == true);

    /* 1 < 5 != 3 < 10 → true != true → false */
    assert(eval_bool_expr_ctx("a < b != c < d", (const char*[]){"a","b","c","d",NULL}, (double[]){1,5,3,10}) == false);

    /* 5 > 3 == 10 >= 10 → true == true → true */
    assert(eval_bool_expr_ctx("a > b == c >= d", (const char*[]){"a","b","c","d",NULL}, (double[]){5,3,10,10}) == true);

    /* 3 <= 3 == 5 >= 6 → true == false → false */
    assert(eval_bool_expr_ctx("a <= b == c >= d", (const char*[]){"a","b","c","d",NULL}, (double[]){3,3,5,6}) == false);

    printf("  ✓ test_relational_before_equality\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Parentheses override precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parentheses_override(void) {
    ASSERT_DOUBLE_EQ(eval_expr("(2 + 3) * 4"), 20.0);
    ASSERT_DOUBLE_EQ(eval_expr("2 * (3 + 4)"), 14.0);
    /* (false or true) and false = true and false = false */
    assert(eval_bool_expr("(false or true) and false") == false);
    printf("  ✓ test_parentheses_override\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Left-to-right associativity for arithmetic
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_left_to_right_arithmetic(void) {
    /* 10 - 3 - 2 = (10-3) - 2 = 5 */
    ASSERT_DOUBLE_EQ(eval_expr("10 - 3 - 2"), 5.0);
    /* 12 / 3 / 2 = (12/3) / 2 = 2 */
    ASSERT_DOUBLE_EQ(eval_expr("12 / 3 / 2"), 2.0);
    printf("  ✓ test_left_to_right_arithmetic\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Complex mixed precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_complex_mixed(void) {
    /* 1 + 2 * 3 ^ 2 = 1 + 2*9 = 1 + 18 = 19 */
    ASSERT_DOUBLE_EQ(eval_expr("1 + 2 * 3 ^ 2"), 19.0);
    /* 2 + 3 < 10 and 5 > 2 = true and true = true */
    assert(eval_bool_expr("2 + 3 < 10 and 5 > 2") == true);
    printf("  ✓ test_complex_mixed\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Nested parentheses
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_nested_parentheses(void) {
    /* Simple nesting */
    ASSERT_DOUBLE_EQ(eval_expr("((2 + 3))"), 5.0);
    ASSERT_DOUBLE_EQ(eval_expr("(((1)))"), 1.0);

    /* Two levels affecting result */
    ASSERT_DOUBLE_EQ(eval_expr("((2 + 3) * (4 + 5))"), 45.0);
    ASSERT_DOUBLE_EQ(eval_expr("(2 * (3 + (4 * 5)))"), 46.0);

    /* Deep nesting: 2*(3+(4*(5+1))) = 2*(3+24) = 2*27 = 54 */
    ASSERT_DOUBLE_EQ(eval_expr("2 * (3 + (4 * (5 + 1)))"), 54.0);

    /* Nested with unary minus */
    ASSERT_DOUBLE_EQ(eval_expr("-(-(3))"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("(-(-(-1)))"), -1.0);

    /* Nested parentheses with comparison and logic */
    assert(eval_bool_expr("((3 > 2) and (1 < 5))") == true);
    assert(eval_bool_expr("not ((3 < 2) or (1 > 5))") == true);

    /* Nested function calls inside parentheses */
    ASSERT_DOUBLE_EQ(eval_expr("(max(1, 2) + min(3, 4)) * 2"), 10.0);
    ASSERT_DOUBLE_EQ(eval_expr("sqrt((3 * 3) + (4 * 4))"), 5.0);

    /* Parentheses overriding multiple levels of precedence */
    /* Without parens: 2 + 3 * 4 ^ 2 = 2 + 3*16 = 50 */
    ASSERT_DOUBLE_EQ(eval_expr("2 + 3 * 4 ^ 2"), 50.0);
    /* With parens: ((2+3)*4)^2 = (20)^2 = 400 */
    ASSERT_DOUBLE_EQ(eval_expr("((2 + 3) * 4) ^ 2"), 400.0);

    printf("  ✓ test_nested_parentheses\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running precedence tests...\n");
    test_mul_before_add();
    test_div_before_sub();
    test_modulo_same_as_mul();
    test_power_right_associative();
    test_power_before_mul();
    test_comparison_after_arithmetic();
    test_and_before_or();
    test_not_precedence();
    test_unary_minus_precedence();
    test_relational_before_equality();
    test_parentheses_override();
    test_left_to_right_arithmetic();
    test_complex_mixed();
    test_nested_parentheses();
    printf("All precedence tests passed!\n");
    return 0;
}
