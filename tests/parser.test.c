/**
 * @file parser.test.c
 * @brief Unit tests for the cxpr parser and AST.
 *
 * Tests covered:
 * - Simple expression parsing (numbers, identifiers)
 * - Binary operators (arithmetic, comparison, logical)
 * - Operator precedence
 * - Parenthesized expressions
 * - Unary operators (negation, not)
 * - Field access (macd.histogram)
 * - Function calls (simple and nested)
 * - Ternary expressions
 * - $variable parsing
 * - Syntax error reporting
 * - Reference extraction (identifiers, functions, $variables)
 * - AST inspection API
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_ast* parse_ok(cxpr_parser* p, const char* expr) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "  FAIL: parse('%s') -> %s at line %zu col %zu\n",
                expr, err.message, err.line, err.column);
    }
    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    return ast;
}

static void parse_fail(cxpr_parser* p, const char* expr) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    assert(ast == NULL);
    assert(err.code != CXPR_OK);
    cxpr_ast_free(ast);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: simple parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_number(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "42");
    assert(cxpr_ast_type(ast) == CXPR_NODE_NUMBER);
    assert(fabs(cxpr_ast_number_value(ast) - 42.0) < 1e-10);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_number\n");
}

static void test_bool(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "true");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BOOL);
    assert(cxpr_ast_bool_value(ast) == true);
    cxpr_ast_free(ast);

    ast = parse_ok(p, "false");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BOOL);
    assert(cxpr_ast_bool_value(ast) == false);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_bool\n");
}

static void test_identifier(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "rsi");
    assert(cxpr_ast_type(ast) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_ast_identifier_name(ast), "rsi") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_identifier\n");
}

static void test_variable(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "$oversold");
    assert(cxpr_ast_type(ast) == CXPR_NODE_VARIABLE);
    assert(strcmp(cxpr_ast_variable_name(ast), "oversold") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_variable\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: binary operators
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_addition(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "a + b");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_right(ast)) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_ast_identifier_name(cxpr_ast_left(ast)), "a") == 0);
    assert(strcmp(cxpr_ast_identifier_name(cxpr_ast_right(ast)), "b") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_addition\n");
}

static void test_comparison(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "rsi < 30");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_right(ast)) == CXPR_NODE_NUMBER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_comparison\n");
}

static void test_logical_and(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "a and b");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_logical_and\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: operator precedence
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_precedence_mul_before_add(void) {
    cxpr_parser* p = cxpr_parser_new();
    /* 2 + 3 * 4 should parse as 2 + (3 * 4), not (2 + 3) * 4 */
    cxpr_ast* ast = parse_ok(p, "2 + 3 * 4");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    /* Left should be number 2 */
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_NUMBER);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_left(ast)) - 2.0) < 1e-10);
    /* Right should be 3 * 4 */
    const cxpr_ast* right = cxpr_ast_right(ast);
    assert(cxpr_ast_type(right) == CXPR_NODE_BINARY_OP);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_left(right)) - 3.0) < 1e-10);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_right(right)) - 4.0) < 1e-10);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_precedence_mul_before_add\n");
}

static void test_precedence_and_before_or(void) {
    cxpr_parser* p = cxpr_parser_new();
    /* a or b and c should parse as a or (b and c) */
    cxpr_ast* ast = parse_ok(p, "a or b and c");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    /* Root is OR */
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_IDENTIFIER);
    /* Right is AND */
    assert(cxpr_ast_type(cxpr_ast_right(ast)) == CXPR_NODE_BINARY_OP);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_precedence_and_before_or\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: parentheses
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parentheses(void) {
    cxpr_parser* p = cxpr_parser_new();
    /* (2 + 3) * 4: root should be MUL */
    cxpr_ast* ast = parse_ok(p, "(2 + 3) * 4");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    /* Left should be the (2 + 3) */
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_BINARY_OP);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_parentheses\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: unary operators
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_unary_minus(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "-x");
    assert(cxpr_ast_type(ast) == CXPR_NODE_UNARY_OP);
    assert(cxpr_ast_type(cxpr_ast_operand(ast)) == CXPR_NODE_IDENTIFIER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_unary_minus\n");
}

static void test_not(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "not x");
    assert(cxpr_ast_type(ast) == CXPR_NODE_UNARY_OP);
    assert(cxpr_ast_type(cxpr_ast_operand(ast)) == CXPR_NODE_IDENTIFIER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_not\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: field access
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_field_access(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd.histogram");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FIELD_ACCESS);
    assert(strcmp(cxpr_ast_field_object(ast), "macd") == 0);
    assert(strcmp(cxpr_ast_field_name(ast), "histogram") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_field_access\n");
}

static void test_field_access_in_expression(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd.histogram > 0");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_FIELD_ACCESS);
    assert(cxpr_ast_type(cxpr_ast_right(ast)) == CXPR_NODE_NUMBER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_field_access_in_expression\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: function calls
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_function_no_args(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "pi()");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(ast), "pi") == 0);
    assert(cxpr_ast_function_argc(ast) == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_no_args\n");
}

static void test_function_one_arg(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "sqrt(x)");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(ast), "sqrt") == 0);
    assert(cxpr_ast_function_argc(ast) == 1);
    assert(cxpr_ast_type(cxpr_ast_function_arg(ast, 0)) == CXPR_NODE_IDENTIFIER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_one_arg\n");
}

static void test_function_two_args(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "cross_above(ema_fast, ema_slow)");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(ast), "cross_above") == 0);
    assert(cxpr_ast_function_argc(ast) == 2);
    assert(cxpr_ast_type(cxpr_ast_function_arg(ast, 0)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_function_arg(ast, 1)) == CXPR_NODE_IDENTIFIER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_two_args\n");
}

static void test_function_nested(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "max(0, min(x, 100))");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(cxpr_ast_function_argc(ast) == 2);
    assert(cxpr_ast_type(cxpr_ast_function_arg(ast, 1)) == CXPR_NODE_FUNCTION_CALL);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_nested\n");
}

static void test_function_field_access(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd(12, 26, 9).signal");
    const char* refs[4] = {0};
    assert(cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(cxpr_ast_references(ast, refs, 4) == 1);
    assert(strcmp(refs[0], "macd.signal") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_field_access\n");
}

static void test_function_named_args(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd(fast=9, slow=21, period=3)");
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(cxpr_ast_function_argc(ast) == 3);
    assert(cxpr_ast_function_has_named_args(ast));
    assert(strcmp(cxpr_ast_function_arg_name(ast, 0), "fast") == 0);
    assert(strcmp(cxpr_ast_function_arg_name(ast, 1), "slow") == 0);
    assert(strcmp(cxpr_ast_function_arg_name(ast, 2), "period") == 0);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_function_arg(ast, 0)) - 9.0) < 1e-10);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_function_arg(ast, 1)) - 21.0) < 1e-10);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_function_arg(ast, 2)) - 3.0) < 1e-10);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_function_named_args\n");
}

static void test_producer_named_args(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd(fast=9, slow=21, period=3).signal");
    assert(cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(cxpr_ast_producer_argc(ast) == 3);
    assert(cxpr_ast_producer_has_named_args(ast));
    assert(strcmp(cxpr_ast_producer_arg_name(ast, 0), "fast") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(ast, 1), "slow") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(ast, 2), "period") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_producer_named_args\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: ternary
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_ternary(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "x > 0 ? x : 0");
    assert(cxpr_ast_type(ast) == CXPR_NODE_TERNARY);
    assert(cxpr_ast_type(cxpr_ast_ternary_condition(ast)) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_type(cxpr_ast_ternary_true_branch(ast)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_ternary_false_branch(ast)) == CXPR_NODE_NUMBER);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_ternary\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: complex expression
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_complex_entry_expression(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "rsi < $oversold and cross_above(ema_fast, ema_slow)");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP); /* AND */
    /* Left: rsi < $oversold */
    const cxpr_ast* left = cxpr_ast_left(ast);
    assert(cxpr_ast_type(left) == CXPR_NODE_BINARY_OP);  /* < */
    assert(cxpr_ast_type(cxpr_ast_left(left)) == CXPR_NODE_IDENTIFIER);  /* rsi */
    assert(cxpr_ast_type(cxpr_ast_right(left)) == CXPR_NODE_VARIABLE);   /* $oversold */
    /* Right: cross_above(ema_fast, ema_slow) */
    const cxpr_ast* right = cxpr_ast_right(ast);
    assert(cxpr_ast_type(right) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(right), "cross_above") == 0);
    assert(cxpr_ast_function_argc(right) == 2);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_complex_entry_expression\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: reference extraction
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_reference_extraction(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "rsi < $oversold and cross_above(ema_fast, ema_slow)");

    /* References: identifiers used (not function names, not $vars) */
    const char* refs[64];
    size_t nrefs = cxpr_ast_references(ast, refs, 64);
    assert(nrefs == 3);
    /* Should contain rsi, ema_fast, ema_slow */
    bool found_rsi = false, found_fast = false, found_slow = false;
    for (size_t i = 0; i < nrefs; i++) {
        if (strcmp(refs[i], "rsi") == 0) found_rsi = true;
        if (strcmp(refs[i], "ema_fast") == 0) found_fast = true;
        if (strcmp(refs[i], "ema_slow") == 0) found_slow = true;
    }
    assert(found_rsi && found_fast && found_slow);

    /* Functions */
    const char* fns[64];
    size_t nfns = cxpr_ast_functions_used(ast, fns, 64);
    assert(nfns == 1);
    assert(strcmp(fns[0], "cross_above") == 0);

    /* Variables */
    const char* vars[64];
    size_t nvars = cxpr_ast_variables_used(ast, vars, 64);
    assert(nvars == 1);
    assert(strcmp(vars[0], "oversold") == 0);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_reference_extraction\n");
}

static void test_reference_extraction_field_access(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = parse_ok(p, "macd.histogram > 0 and adx.adx > 25");

    const char* refs[64];
    size_t nrefs = cxpr_ast_references(ast, refs, 64);
    assert(nrefs == 2);
    bool found_macd = false, found_adx = false;
    for (size_t i = 0; i < nrefs; i++) {
        if (strcmp(refs[i], "macd.histogram") == 0) found_macd = true;
        if (strcmp(refs[i], "adx.adx") == 0) found_adx = true;
    }
    assert(found_macd && found_adx);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_reference_extraction_field_access\n");
}

static void test_reference_deduplication(void) {
    cxpr_parser* p = cxpr_parser_new();
    /* rsi appears twice */
    cxpr_ast* ast = parse_ok(p, "rsi < 30 or rsi > 70");
    const char* refs[64];
    size_t nrefs = cxpr_ast_references(ast, refs, 64);
    assert(nrefs == 1); /* deduplicated */
    assert(strcmp(refs[0], "rsi") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_reference_deduplication\n");
}

static double stub_cross(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    (void)userdata;
    return 0.0;
}

static void test_analysis_metadata(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};

    cxpr_registry_add(reg, "cross_above", stub_cross, 2, 2, NULL, NULL);
    cxpr_ast* ast = parse_ok(p, "rsi < $oversold and cross_above(ema_fast, ema_slow)");

    assert(cxpr_analyze(ast, reg, &info, &err));
    assert(err.code == CXPR_OK);
    assert(info.result_type == CXPR_EXPR_BOOL);
    assert(info.is_constant == false);
    assert(info.is_predicate == true);
    assert(info.uses_variables == true);
    assert(info.uses_parameters == true);
    assert(info.uses_functions == true);
    assert(info.uses_expressions == false);
    assert(info.uses_field_access == false);
    assert(info.can_short_circuit == true);
    assert(info.node_count == 7);
    assert(info.max_depth == 3);
    assert(info.reference_count == 3);
    assert(info.function_count == 1);
    assert(info.parameter_count == 1);
    assert(info.field_path_count == 0);
    assert(info.has_unknown_functions == false);
    assert(info.first_unknown_function == NULL);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_analysis_metadata\n");
}

static void test_analysis_field_paths(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_analysis info;
    cxpr_error err = {0};
    cxpr_ast* ast = parse_ok(p, "macd.histogram > 0 and adx.adx > 25");

    assert(cxpr_analyze(ast, NULL, &info, &err));
    assert(info.uses_field_access == true);
    assert(info.field_path_count == 2);
    assert(info.reference_count == 2);
    assert(info.result_type == CXPR_EXPR_BOOL);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_analysis_field_paths\n");
}

static void test_analysis_unknown_function(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};
    cxpr_ast* ast = parse_ok(p, "missing_fn(x)");

    assert(!cxpr_analyze(ast, reg, &info, &err));
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    assert(info.has_unknown_functions == true);
    assert(info.first_unknown_function != NULL);
    assert(strcmp(info.first_unknown_function, "missing_fn") == 0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_analysis_unknown_function\n");
}

static void test_analysis_wrong_arity(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};

    cxpr_register_builtins(reg);
    cxpr_ast* ast = parse_ok(p, "sqrt(1, 2)");

    assert(!cxpr_analyze(ast, reg, &info, &err));
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_analysis_wrong_arity\n");
}

static void test_analysis_named_args_reorders_by_signature(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};
    const char* params[] = {"slow", "fast", "period"};

    cxpr_registry_add(reg, "macd", stub_cross, 3, 3, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", params, 3));
    cxpr_ast* ast = parse_ok(p, "macd(fast=9, slow=21, period=3)");

    assert(cxpr_analyze(ast, reg, &info, &err));
    assert(err.code == CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
    printf("  ✓ test_analysis_named_args_reorders_by_signature\n");
}

static void test_analyze_expr_convenience_api(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_analysis info;
    cxpr_error err = {0};

    cxpr_registry_add(reg, "cross_above", stub_cross, 2, 2, NULL, NULL);
    assert(cxpr_analyze_expr("rsi < $oversold and cross_above(ema_fast, ema_slow)",
                             reg, &info, &err));
    assert(err.code == CXPR_OK);
    assert(info.result_type == CXPR_EXPR_BOOL);
    assert(info.uses_variables == true);
    assert(info.uses_parameters == true);
    assert(info.uses_functions == true);
    assert(info.function_count == 1);
    assert(info.reference_count == 3);
    assert(info.parameter_count == 1);

    cxpr_registry_free(reg);
    printf("  ✓ test_analyze_expr_convenience_api\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: syntax errors
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_syntax_errors(void) {
    cxpr_parser* p = cxpr_parser_new();

    parse_fail(p, "");       /* empty expression */
    parse_fail(p, "(");      /* unclosed paren */
    parse_fail(p, "a +");    /* missing operand */
    parse_fail(p, "a b");    /* two identifiers without operator */
    parse_fail(p, "f(a,)");  /* trailing comma in func args */
    parse_fail(p, ".");      /* lone dot */
    parse_fail(p, "f(1).");  /* missing field after function call */

    cxpr_parser_free(p);
    printf("  ✓ test_syntax_errors\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: power right-associativity
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_power_right_associative(void) {
    cxpr_parser* p = cxpr_parser_new();
    /* 2^3^2 should parse as 2^(3^2), not (2^3)^2 */
    cxpr_ast* ast = parse_ok(p, "2 ^ 3 ^ 2");
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    /* Left is 2 */
    assert(cxpr_ast_type(cxpr_ast_left(ast)) == CXPR_NODE_NUMBER);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_left(ast)) - 2.0) < 1e-10);
    /* Right is 3^2 (binary op) */
    const cxpr_ast* right = cxpr_ast_right(ast);
    assert(cxpr_ast_type(right) == CXPR_NODE_BINARY_OP);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_left(right)) - 3.0) < 1e-10);
    assert(fabs(cxpr_ast_number_value(cxpr_ast_right(right)) - 2.0) < 1e-10);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  ✓ test_power_right_associative\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running parser tests...\n");

    /* Simple */
    test_number();
    test_bool();
    test_identifier();
    test_variable();

    /* Binary ops */
    test_addition();
    test_comparison();
    test_logical_and();

    /* Precedence */
    test_precedence_mul_before_add();
    test_precedence_and_before_or();
    test_power_right_associative();

    /* Parentheses */
    test_parentheses();

    /* Unary */
    test_unary_minus();
    test_not();

    /* Field access */
    test_field_access();
    test_field_access_in_expression();

    /* Functions */
    test_function_no_args();
    test_function_one_arg();
    test_function_two_args();
    test_function_nested();
    test_function_field_access();
    test_function_named_args();
    test_producer_named_args();

    /* Ternary */
    test_ternary();

    /* Complex expression */
    test_complex_entry_expression();

    /* Reference extraction */
    test_reference_extraction();
    test_reference_extraction_field_access();
    test_reference_deduplication();
    test_analysis_metadata();
    test_analysis_field_paths();
    test_analysis_unknown_function();
    test_analysis_wrong_arity();
    test_analysis_named_args_reorders_by_signature();
    test_analyze_expr_convenience_api();

    /* Errors */
    test_syntax_errors();

    printf("All parser tests passed!\n");
    return 0;
}
