#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_parser_grammar_expression_forms(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;

    assert(p);

    ast = cxpr_parse(p, "x |> clamp(0, 1)", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(cxpr_ast_function_argc(ast) == 3);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "score in [10, 20]", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_operator(ast) == CXPR_TOK_AND);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "a ? b : c", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_TERNARY);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "2^3^2", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_ast_operator(ast) == CXPR_TOK_POWER);
    assert(cxpr_ast_type(cxpr_ast_right(ast)) == CXPR_NODE_BINARY_OP);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
}

int main(void) {
    test_parser_grammar_expression_forms();
    printf("  \xE2\x9C\x93 parser_grammar_expr\n");
    return 0;
}
