#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_public_ast_inspection_helpers(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* binary = cxpr_ast_new_binary_op(CXPR_TOK_PLUS,
                                              cxpr_ast_new_identifier("left"),
                                              cxpr_ast_new_number(2.0));
    cxpr_ast* unary = cxpr_ast_new_unary_op(CXPR_TOK_MINUS, cxpr_ast_new_number(3.0));
    cxpr_ast* field = cxpr_ast_new_field_access("quote", "mid");
    cxpr_ast* string;

    assert(parser);
    string = cxpr_parse(parser, "\"hello\"", &err);

    assert(binary);
    assert(unary);
    assert(field);
    assert(string);

    assert(cxpr_ast_operator(binary) == CXPR_TOK_PLUS);
    assert(cxpr_ast_type(cxpr_ast_left(binary)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_ast_type(cxpr_ast_right(binary)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_operator(unary) == CXPR_TOK_MINUS);
    assert(cxpr_ast_type(cxpr_ast_operand(unary)) == CXPR_NODE_NUMBER);
    assert(strcmp(cxpr_ast_field_object(field), "quote") == 0);
    assert(strcmp(cxpr_ast_field_name(field), "mid") == 0);
    assert(strcmp(cxpr_ast_string_value(string), "hello") == 0);

    cxpr_ast_free(string);
    cxpr_ast_free(field);
    cxpr_ast_free(unary);
    cxpr_ast_free(binary);
    cxpr_parser_free(parser);
}

int main(void) {
    test_public_ast_inspection_helpers();
    printf("  \xE2\x9C\x93 ast_inspect\n");
    return 0;
}
