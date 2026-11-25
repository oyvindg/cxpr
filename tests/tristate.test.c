/**
 * @file tristate.test.c
 * @brief Unit tests for the generic Kleene three-valued lowering.
 *
 * The classifier here is deliberately domain-neutral: a leaf is KNOWN iff its
 * rendered name begins with "k_". This keeps the cxpr test free of any host or
 * trading meaning; the resample/timeframe/position policy is exercised by dyn.
 */

#include <cxpr/tristate.h>
#include <cxpr/expr/ast.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Known iff the leaf references an identifier/field whose root name starts
   with "k_"; literals are handled by the transform itself. */
static cxpr_tristate_knownness classify(const cxpr_expr_ast* leaf, void* ud) {
    const char* name = NULL;
    (void)ud;
    switch (cxpr_expr_ast_kind_of(leaf)) {
        case CXPR_NODE_IDENTIFIER: name = cxpr_expr_ast_identifier_name(leaf); break;
        case CXPR_NODE_VARIABLE:   return CXPR_TRISTATE_KNOWN; /* params are constants */
        case CXPR_NODE_FIELD_ACCESS: name = cxpr_expr_ast_field_object(leaf); break;
        default: break;
    }
    if (name && strncmp(name, "k_", 2) == 0) return CXPR_TRISTATE_KNOWN;
    return CXPR_TRISTATE_UNKNOWN;
}

static cxpr_expr_ast* parse(const char* expr) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, expr, &err);
    assert(ast != NULL);
    cxpr_expr_parser_free(parser);
    return ast;
}

static char* canon(const char* expr) {
    cxpr_expr_ast* ast = parse(expr);
    char* s = cxpr_expr_ast_to_string(ast);
    cxpr_expr_ast_free(ast);
    return s;
}

static cxpr_expr_ast* lower(const char* expr) {
    cxpr_expr_ast* in = parse(expr);
    cxpr_expr_ast* out = cxpr_tristate_definitely_true(in, classify, NULL);
    cxpr_expr_ast_free(in);
    assert(out != NULL);
    return out;
}

static void expect_lowers_to(const char* expr, const char* expected) {
    cxpr_expr_ast* out = lower(expr);
    char* got = cxpr_expr_ast_to_string(out);
    char* want = canon(expected);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "tristate lowering mismatch\n  expr:     %s\n  got:      %s\n  expected: %s\n",
                expr, got, want);
        assert(0);
    }
    free(got);
    free(want);
    cxpr_expr_ast_free(out);
}

static void expect_blocked(const char* expr) {
    cxpr_expr_ast* out = lower(expr);
    cxpr_expr_ast* in;
    assert(cxpr_expr_ast_kind_of(out) == CXPR_NODE_BOOL);
    assert(cxpr_expr_ast_bool_value(out) == false);
    cxpr_expr_ast_free(out);
    in = parse(expr);
    assert(cxpr_tristate_has_true_term(in, classify, NULL) == false);
    cxpr_expr_ast_free(in);
}

static void expect_eligible(const char* expr) {
    cxpr_expr_ast* in = parse(expr);
    assert(cxpr_tristate_has_true_term(in, classify, NULL) == true);
    cxpr_expr_ast_free(in);
}

int main(void) {
    /* A pure known comparison is itself the definitely-true condition. */
    expect_lowers_to("k_low <= 100", "k_low <= 100");

    /* A pure unknown comparison can never be definitely true. */
    expect_blocked("close <= 100");

    /* OR: the known term survives; the unknown term drops out. */
    expect_lowers_to("k_low <= 100 or close <= 50", "k_low <= 100");

    /* AND: the whole conjunction is blocked by the unknown term. */
    expect_blocked("k_low <= 100 and close <= 50");

    /* NOT of an unknown stays unknown: no spurious definitely-true. */
    expect_blocked("not (close <= 50)");

    /* NOT of a known is itself known. */
    expect_lowers_to("not (k_low <= 50)", "not (k_low <= 50)");

    /* Arithmetic over knowns stays known; a mixed operand poisons it. */
    expect_lowers_to("k_low <= k_entry * 0.98", "k_low <= k_entry * 0.98");
    expect_blocked("k_low <= close * 0.98");

    /* $param is a runtime constant, hence known. */
    expect_lowers_to("k_low <= $stop", "k_low <= $stop");

    /* Mixed OR keeps only the known disjunct. */
    expect_lowers_to("k_low <= 90 or close < 50 or close > 200", "k_low <= 90");

    /* Ternary: definitely-true only when a known branch is selected by a known
       condition; here the condition is unknown so the whole thing is blocked. */
    expect_blocked("(close > 0) ? (k_low <= 1) : (k_low <= 2)");
    /* Known condition selecting known branches is recoverable. */
    expect_lowers_to("(k_flag > 0) ? (k_low <= 1) : (k_low <= 2)",
                     "(k_flag > 0 and k_low <= 1) or (not (k_flag > 0) and k_low <= 2)");

    expect_eligible("k_low <= 100 or close <= 50");

    /* is_known: fully-known value vs one poisoned by an unknown leaf. */
    {
        cxpr_expr_ast* known = parse("k_entry * 0.98 - k_low");
        cxpr_expr_ast* mixed = parse("k_entry * close");
        assert(cxpr_tristate_is_known(known, classify, NULL) == true);
        assert(cxpr_tristate_is_known(mixed, classify, NULL) == false);
        cxpr_expr_ast_free(known);
        cxpr_expr_ast_free(mixed);
    }

    printf("tristate: all tests passed\n");
    return 0;
}
