#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static cxpr_ast* parse_expr(const char* expression) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    assert(parser != NULL);
    ast = cxpr_parse(parser, expression, &err);
    cxpr_parser_free(parser);
    return ast;
}

static void test_public_ast_inspection_helpers(void) {
    cxpr_ast* binary = cxpr_ast_new_binary_op(CXPR_TOK_PLUS,
                                              cxpr_ast_new_identifier("left"),
                                              cxpr_ast_new_number(2.0));
    cxpr_ast* unary = cxpr_ast_new_unary_op(CXPR_TOK_MINUS, cxpr_ast_new_number(3.0));
    cxpr_ast* field = cxpr_ast_new_field_access("quote", "mid");
    cxpr_ast* predicate;
    cxpr_ast* scalar_call;
    cxpr_ast* cross_call;

    predicate = parse_expr("rsi(14) > 70");
    scalar_call = parse_expr("ema(14)");
    cross_call = parse_expr("cross_above(ema(14), sma(50))");

    assert(binary);
    assert(unary);
    assert(field);
    assert(predicate);
    assert(scalar_call);
    assert(cross_call);

    assert(cxpr_ast_operator(binary) == CXPR_TOK_PLUS);
    assert(cxpr_ast_type(cxpr_ast_left(binary)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_right(binary)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_operator(unary) == CXPR_TOK_MINUS);
    assert(cxpr_ast_type(cxpr_ast_operand(unary)) == CXPR_NODE_NUMBER);
    assert(strcmp(cxpr_ast_field_object(field), "quote") == 0);
    assert(strcmp(cxpr_ast_field_name(field), "mid") == 0);
    assert(cxpr_ast_is_boolean_expression(predicate));
    assert(!cxpr_ast_is_boolean_expression(scalar_call));
    assert(cxpr_ast_is_boolean_expression(cross_call));

    cxpr_ast_free(cross_call);
    cxpr_ast_free(scalar_call);
    cxpr_ast_free(predicate);
    cxpr_ast_free(field);
    cxpr_ast_free(unary);
    cxpr_ast_free(binary);
}

int main(void) {
    test_public_ast_inspection_helpers();
    printf("  \xE2\x9C\x93 ast_inspect\n");
    return 0;
}
