/**
 * @file errors.test.c
 * @brief Unit tests for error handling and reporting.
 *
 * Tests covered:
 * - Parse error: unexpected token at position
 * - Parse error: unclosed parenthesis
 * - Parse error: missing operand
 * - Parse error: trailing content after expression
 * - Eval error: unknown identifier with correct code
 * - Eval error: unknown function
 * - Eval error: wrong arity
 * - Eval error: division by zero
 * - Eval error: unknown parameter
 * - Error string function
 * - Position tracking in errors
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cxpr_test_internal.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: cxpr_error_string
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_error_strings(void) {
    assert(strcmp(cxpr_error_string(CXPR_OK), "OK") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_SYNTAX), "Syntax error") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_UNKNOWN_IDENTIFIER), "Unknown identifier") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_UNKNOWN_FUNCTION), "Unknown function") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_WRONG_ARITY), "Wrong number of arguments") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_DIVISION_BY_ZERO), "Division by zero") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_CIRCULAR_DEPENDENCY), "Circular dependency") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_TYPE_MISMATCH), "Type mismatch") == 0);
    assert(strcmp(cxpr_error_string(CXPR_ERR_OUT_OF_MEMORY), "Out of memory") == 0);
    printf("  ✓ test_error_strings\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: cxpr_error_format
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_error_format(void) {
    char buf[256];
    size_t n;

    /* No-error / NULL cases yield "no error". */
    n = cxpr_error_format(NULL, buf, sizeof(buf));
    assert(strcmp(buf, "no error") == 0);
    assert(n == strlen("no error"));
    cxpr_error ok = {0};
    cxpr_error_format(&ok, buf, sizeof(buf));
    assert(strcmp(buf, "no error") == 0);

    /* Positioned error includes "code at line:column: message". */
    cxpr_error pos = { CXPR_ERR_SYNTAX, "Expected ')'", 6u, 1u, 7u };
    cxpr_error_format(&pos, buf, sizeof(buf));
    assert(strcmp(buf, "Syntax error at 1:7: Expected ')'") == 0);

    /* Error without position omits the location. */
    cxpr_error nopos = { CXPR_ERR_TYPE_MISMATCH, "bad operand", 0u, 0u, 0u };
    cxpr_error_format(&nopos, buf, sizeof(buf));
    assert(strcmp(buf, "Type mismatch: bad operand") == 0);

    /* NULL message falls back to the code string. */
    cxpr_error nomsg = { CXPR_ERR_DIVISION_BY_ZERO, NULL, 0u, 0u, 0u };
    cxpr_error_format(&nomsg, buf, sizeof(buf));
    assert(strcmp(buf, "Division by zero: Division by zero") == 0);

    /* Return value reports the full length even when truncated; output stays
     * NUL-terminated within the provided capacity. */
    char small[8];
    n = cxpr_error_format(&pos, small, sizeof(small));
    assert(n == strlen("Syntax error at 1:7: Expected ')'"));
    assert(strlen(small) == sizeof(small) - 1u);

    /* Size 0 with NULL buffer is allowed and still reports the length. */
    n = cxpr_error_format(&nopos, NULL, 0u);
    assert(n == strlen("Type mismatch: bad operand"));

    printf("  ✓ test_error_format\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — empty expression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_empty(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_empty\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — unclosed parenthesis
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_unclosed_paren(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "(2 + 3", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_unclosed_paren\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — missing operand
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_missing_operand(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "3 +", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_missing_operand\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — trailing content
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_trailing_content(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "3 4", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_trailing_content\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — missing function closing paren
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_unclosed_func(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "sqrt(4", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_unclosed_func\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — missing ternary colon
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_missing_ternary_colon(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "x > 0 ? 1", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_missing_ternary_colon\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error — dollar without identifier
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_bare_dollar(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "$ + 1", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    cxpr_parser_free(p);
    printf("  ✓ test_parse_error_bare_dollar\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: eval error — unknown identifier
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_eval_error_unknown_identifier(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "unknown_var + 1", &err);
    assert(ast != NULL);

    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_error_unknown_identifier\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: eval error — unknown parameter
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_eval_error_unknown_param(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "$missing_param", &err);
    assert(ast != NULL);

    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_error_unknown_param\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: eval error — unknown function
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_eval_error_unknown_function(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "foobar(1, 2)", &err);
    assert(ast != NULL);

    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_error_unknown_function\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: eval error — wrong arity
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_eval_error_wrong_arity(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    /* sqrt takes 1 arg, not 3 */
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "sqrt(1, 2, 3)", &err);
    assert(ast != NULL);

    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_expr_ast_free(ast);

    /* min takes 2 args, not 0 */
    ast = cxpr_expr_ast_parse(p, "min()", &err);
    assert(ast != NULL);

    err = (cxpr_error){0};
    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_error_wrong_arity\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: eval error — division by zero
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_eval_error_division_by_zero(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_error err = {0};

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "10 / 0", &err);
    assert(ast != NULL);

    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_DIVISION_BY_ZERO);
    assert(err.message != NULL);

    cxpr_expr_ast_free(ast);

    /* Modulo by zero */
    ast = cxpr_expr_ast_parse(p, "10 % 0", &err);
    assert(ast != NULL);

    err = (cxpr_error){0};
    cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_ERR_DIVISION_BY_ZERO);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_eval_error_division_by_zero\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse error position tracking
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_error_position(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};

    /* Error should be at position of unexpected token */
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "a @", &err);
    assert(ast == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.line == 1);
    assert(err.column > 1); /* @ is not at column 1 */

    cxpr_parser_free(p);
    printf("  ✓ test_error_position\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parse errors mid-production must not leak partial AST nodes
 *
 * Each input parses far enough to allocate one or more AST nodes, then hits an
 * invalid byte (0xA0) or premature end. The parser must free every partial node
 * before returning NULL. Run under the asan preset, a regression here surfaces
 * as a LeakSanitizer failure. Found originally by the libFuzzer harness.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parse_error_no_partial_leak(void) {
    /* BAD isolates the invalid 0xA0 byte so the next character is not folded
     * into the hex escape. */
#define BAD "\xA0"
    static const char* const cases[] = {
        "rsi < 30 and volume" BAD "z",  /* binary right operand */
        "a or b" BAD "z",               /* logical-or right */
        "a == b" BAD "z",               /* equality right */
        "a + b" BAD "z",                /* arithmetic right */
        "a * b" BAD "z",                /* term right */
        "a ^ b" BAD "z",                /* power right */
        "not a" BAD,                    /* not operand */
        "-a" BAD,                       /* unary operand */
        "a ? b" BAD "z : d",            /* ternary true branch */
        "a ? b : c" BAD "z",            /* ternary false branch */
        "x |> f" BAD,                   /* pipe stage */
        "clamp(a, b" BAD,               /* call argument */
        "f(x=" BAD,                     /* named-argument value */
        "macd(12, 26).hist" BAD,        /* producer field access */
        "close[" BAD,                   /* lookback offset */
        "x in [1, 2" BAD,               /* interval bound */
        "(a + b" BAD,                   /* parenthesised group */
    };
#undef BAD

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        cxpr_parser* p = cxpr_parser_new();
        cxpr_error err = {0};
        cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, cases[i], &err);
        assert(ast == NULL);
        assert(err.code != CXPR_OK);
        cxpr_parser_free(p);
    }
    printf("  ✓ test_parse_error_no_partial_leak\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: expression evaluator errors
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_formula_parse_error(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    /* Invalid expression */
    bool ok = cxpr_expression_add(evaluator, "bad", "3 +", &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_SYNTAX);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_parse_error\n");
}

static void test_formula_circular_error(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "a", "b + 1", &err));
    assert(cxpr_expression_add(evaluator, "b", "a + 1", &err));

    bool ok = cxpr_evaluator_compile(evaluator, &err);
    assert(!ok);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);
    assert(err.message != NULL);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_circular_error\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: registry lookup for validation
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_registry_lookup(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_defaults(reg);

    /* Built-in functions should be findable */
    size_t min_a, max_a;
    assert(cxpr_registry_lookup(reg, "sqrt", &min_a, &max_a));
    assert(min_a == 1 && max_a == 1);

    assert(cxpr_registry_lookup(reg, "min", &min_a, &max_a));
    assert(min_a == 1 && max_a == 8);

    assert(cxpr_registry_lookup(reg, "max", &min_a, &max_a));
    assert(min_a == 1 && max_a == 8);

    assert(cxpr_registry_lookup(reg, "clamp", &min_a, &max_a));
    assert(min_a == 3 && max_a == 3);

    assert(cxpr_registry_lookup(reg, "pi", &min_a, &max_a));
    assert(min_a == 0 && max_a == 0);

    /* Non-existent function */
    assert(!cxpr_registry_lookup(reg, "nonexistent", &min_a, &max_a));

    cxpr_registry_free(reg);
    printf("  ✓ test_registry_lookup\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running error tests...\n");

    test_error_strings();
    test_error_format();

    /* Parse errors */
    test_parse_error_empty();
    test_parse_error_unclosed_paren();
    test_parse_error_missing_operand();
    test_parse_error_trailing_content();
    test_parse_error_unclosed_func();
    test_parse_error_missing_ternary_colon();
    test_parse_error_bare_dollar();

    /* Eval errors */
    test_eval_error_unknown_identifier();
    test_eval_error_unknown_param();
    test_eval_error_unknown_function();
    test_eval_error_wrong_arity();
    test_eval_error_division_by_zero();

    /* Error position tracking */
    test_error_position();
    test_parse_error_no_partial_leak();

    /* Expression evaluator errors */
    test_formula_parse_error();
    test_formula_circular_error();

    /* Registry lookup */
    test_registry_lookup();

    printf("All error tests passed!\n");
    return 0;
}
