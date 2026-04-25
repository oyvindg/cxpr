/**
 * @file lexer.test.c
 * @brief Unit tests for the cxpr lexer.
 *
 * Tests covered:
 * - Number tokenization (integer, decimal, scientific)
 * - Operator tokenization (single-char and two-char)
 * - Keyword aliases (and/AND, or/OR, not/NOT, eq, lt, etc.)
 * - Boolean literals (true/false/TRUE/FALSE)
 * - Identifier and $variable tokenization
 * - Dot token for field access
 * - Ternary tokens (? and :)
 * - Position tracking (line, column)
 * - Error cases (invalid characters)
 */

#include "cxpr_test_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: numbers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_integer(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "42");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 42.0);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_integer\n");
}

static void test_decimal(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "3.14");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 3.14);
    printf("  ✓ test_decimal\n");
}

static void test_scientific(void) {
    cxpr_lexer lex;

    cxpr_lexer_init(&lex, "1e3");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 1000.0);

    cxpr_lexer_init(&lex, "1.5e-3");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 0.0015);

    cxpr_lexer_init(&lex, "2E+4");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 20000.0);

    printf("  ✓ test_scientific\n");
}

static void test_zero(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "0");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    ASSERT_DOUBLE_EQ(tok.number_value, 0.0);
    printf("  ✓ test_zero\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: operators
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_arithmetic_operators(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "+ - * / % ^");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_PLUS);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_MINUS);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_STAR);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_SLASH);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_PERCENT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_POWER);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_arithmetic_operators\n");
}

static void test_comparison_operators(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "== != < > <= >=");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NEQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_comparison_operators\n");
}

static void test_assign_vs_eq_tokens(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "= ==");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_ASSIGN);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_assign_vs_eq_tokens\n");
}

static void test_logical_operators(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "&& || !");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_AND);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_OR);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NOT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_logical_operators\n");
}

static void test_power_double_star(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "**");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_POWER);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_power_double_star\n");
}

static void test_pipe_operator(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "a |> f");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_PIPE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_pipe_operator\n");
}

static void test_pipe_gt_operator_combinations(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "a |> f > g >= h || i |> j ||> k");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* a */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_PIPE);       /* |> */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* f */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GT);         /* > */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* g */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);        /* >= */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* h */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_OR);         /* || */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* i */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_PIPE);       /* |> */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* j */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_OR);         /* || (from ||>) */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GT);         /* >  (from ||>) */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_IDENTIFIER); /* k */
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_pipe_gt_operator_combinations\n");
}

static void test_delimiters(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "( ) , . ? :");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LPAREN);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_RPAREN);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_COMMA);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_DOT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_QUESTION);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_COLON);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);
    printf("  ✓ test_delimiters\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: keywords
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_keyword_and(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "and AND");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_AND);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_AND);
    printf("  ✓ test_keyword_and\n");
}

static void test_keyword_or(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "or OR");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_OR);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_OR);
    printf("  ✓ test_keyword_or\n");
}

static void test_keyword_not(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "not NOT");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NOT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NOT);
    printf("  ✓ test_keyword_not\n");
}

static void test_keyword_comparison_aliases(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "eq EQ ne NE neq NEQ lt LT gt GT le LE lte LTE ge GE gte GTE");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NEQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NEQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NEQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_NEQ);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GT);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_LTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_GTE);
    printf("  ✓ test_keyword_comparison_aliases\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: boolean literals
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_boolean_literals(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "true");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_TRUE);

    cxpr_lexer_init(&lex, "TRUE");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_TRUE);

    cxpr_lexer_init(&lex, "false");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_FALSE);

    cxpr_lexer_init(&lex, "FALSE");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_FALSE);

    printf("  ✓ test_boolean_literals\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: identifiers
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_identifiers(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "rsi ema_fast close_price x1");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 3 && memcmp(tok.start, "rsi", 3) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 8 && memcmp(tok.start, "ema_fast", 8) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 11 && memcmp(tok.start, "close_price", 11) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 2 && memcmp(tok.start, "x1", 2) == 0);

    printf("  ✓ test_identifiers\n");
}

static void test_identifier_not_keyword(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    /* "android" starts with "and" but is an identifier, not a keyword */
    cxpr_lexer_init(&lex, "android order nothing");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 7);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 5);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 7);

    printf("  ✓ test_identifier_not_keyword\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: variables ($name)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_variables(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "$oversold $threshold $min_volume");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_VARIABLE);
    assert(tok.length == 8 && memcmp(tok.start, "oversold", 8) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_VARIABLE);
    assert(tok.length == 9 && memcmp(tok.start, "threshold", 9) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_VARIABLE);
    assert(tok.length == 10 && memcmp(tok.start, "min_volume", 10) == 0);

    printf("  ✓ test_variables\n");
}

static void test_dotted_variable(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "$breakout.range_pct_min");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_VARIABLE);
    assert(tok.length == 22 && memcmp(tok.start, "breakout.range_pct_min", 22) == 0);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_EOF);

    printf("  ✓ test_dotted_variable\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: position tracking
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_position_tracking(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "a + b");
    tok = cxpr_lexer_next(&lex);
    assert(tok.line == 1 && tok.column == 1);

    tok = cxpr_lexer_next(&lex);
    assert(tok.line == 1 && tok.column == 3);

    tok = cxpr_lexer_next(&lex);
    assert(tok.line == 1 && tok.column == 5);

    printf("  ✓ test_position_tracking\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: complex expression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_complex_expression(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "rsi < $oversold and cross_above(ema_fast, ema_slow)");

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER); /* rsi */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_LT);         /* < */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_VARIABLE);   /* $oversold */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_AND);        /* and */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER); /* cross_above */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_LPAREN);     /* ( */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER); /* ema_fast */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_COMMA);      /* , */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER); /* ema_slow */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_RPAREN);     /* ) */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_EOF);

    printf("  ✓ test_complex_expression\n");
}

static void test_ternary_expression(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "x > 0 ? x : 0");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_GT);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_QUESTION);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_COLON);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_NUMBER);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_EOF);

    printf("  ✓ test_ternary_expression\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: error cases
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_error_unknown_char(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "@");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_ERROR);
    printf("  ✓ test_error_unknown_char\n");
}

static void test_error_dollar_alone(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "$ ");
    cxpr_token tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_ERROR);
    printf("  ✓ test_error_dollar_alone\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: peek
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_peek(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "a + b");
    cxpr_token tok = cxpr_lexer_peek(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);

    /* Peek should not advance */
    tok = cxpr_lexer_peek(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);

    /* Now actually advance */
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_PLUS);

    printf("  ✓ test_peek\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: empty input
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_empty_input(void) {
    cxpr_lexer lex;
    cxpr_lexer_init(&lex, "");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);

    cxpr_lexer_init(&lex, "   ");
    assert(cxpr_lexer_next(&lex).type == CXPR_TOK_EOF);

    printf("  ✓ test_empty_input\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: field access tokens
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_field_access_tokens(void) {
    cxpr_lexer lex;
    cxpr_token tok;

    cxpr_lexer_init(&lex, "macd.histogram");
    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 4 && memcmp(tok.start, "macd", 4) == 0);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_DOT);

    tok = cxpr_lexer_next(&lex);
    assert(tok.type == CXPR_TOK_IDENTIFIER);
    assert(tok.length == 9 && memcmp(tok.start, "histogram", 9) == 0);

    printf("  ✓ test_field_access_tokens\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running lexer tests...\n");

    /* Numbers */
    test_integer();
    test_decimal();
    test_scientific();
    test_zero();

    /* Operators */
    test_arithmetic_operators();
    test_comparison_operators();
    test_assign_vs_eq_tokens();
    test_logical_operators();
    test_power_double_star();
    test_pipe_operator();
    test_pipe_gt_operator_combinations();
    test_delimiters();

    /* Keywords */
    test_keyword_and();
    test_keyword_or();
    test_keyword_not();
    test_keyword_comparison_aliases();

    /* Booleans */
    test_boolean_literals();

    /* Identifiers */
    test_identifiers();
    test_identifier_not_keyword();

    /* Variables */
    test_variables();
    test_dotted_variable();

    /* Position */
    test_position_tracking();

    /* Complex expressions */
    test_complex_expression();
    test_ternary_expression();

    /* Field access */
    test_field_access_tokens();

    /* Errors */
    test_error_unknown_char();
    test_error_dollar_alone();

    /* Peek */
    test_peek();

    /* Empty */
    test_empty_input();

    printf("All lexer tests passed!\n");
    return 0;
}
