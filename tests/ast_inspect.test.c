#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cxpr_expr_ast* parse_expr(const char* expression) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, expression, &err);
    cxpr_parser_free(parser);
    return ast;
}

static void test_public_ast_inspection_helpers(void) {
    cxpr_expr_ast* binary = cxpr_expr_ast_binary_new(CXPR_TOK_PLUS,
                                              cxpr_expr_ast_identifier_new("left"),
                                              cxpr_expr_ast_number_new(2.0));
    cxpr_expr_ast* unary = cxpr_expr_ast_unary_new(CXPR_TOK_MINUS, cxpr_expr_ast_number_new(3.0));
    cxpr_expr_ast* field = cxpr_expr_ast_field_new("quote", "mid");
    cxpr_expr_ast* predicate;
    cxpr_expr_ast* scalar_call;
    cxpr_expr_ast* cross_call;
    cxpr_expr_ast* bool_ternary;

    predicate = parse_expr("rsi(14) > 70");
    scalar_call = parse_expr("ema(14)");
    cross_call = parse_expr("cross_above(ema(14), sma(50))");
    bool_ternary = parse_expr("rsi > 50 ? close > open : volume > 0");

    assert(binary);
    assert(unary);
    assert(field);
    assert(predicate);
    assert(scalar_call);
    assert(cross_call);
    assert(bool_ternary);

    assert(cxpr_expr_ast_operator(binary) == CXPR_TOK_PLUS);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_binary_left(binary)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_binary_right(binary)) == CXPR_NODE_NUMBER);
    assert(cxpr_expr_ast_operator(unary) == CXPR_TOK_MINUS);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_unary_operand(unary)) == CXPR_NODE_NUMBER);
    assert(strcmp(cxpr_expr_ast_field_object(field), "quote") == 0);
    assert(strcmp(cxpr_expr_ast_field_name(field), "mid") == 0);
    assert(cxpr_expr_ast_is_boolean_expression(predicate));
    assert(!cxpr_expr_ast_is_boolean_expression(scalar_call));
    assert(cxpr_expr_ast_is_boolean_expression(cross_call));
    assert(cxpr_expr_ast_is_boolean_expression(bool_ternary));

    cxpr_expr_ast_free(bool_ternary);
    cxpr_expr_ast_free(cross_call);
    cxpr_expr_ast_free(scalar_call);
    cxpr_expr_ast_free(predicate);
    cxpr_expr_ast_free(field);
    cxpr_expr_ast_free(unary);
    cxpr_expr_ast_free(binary);
}

int main(void) {
    test_public_ast_inspection_helpers();
    printf("  \xE2\x9C\x93 ast_inspect\n");
    return 0;
}
