#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_parser_expression_forms(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;

    assert(p);

    ast = cxpr_expr_ast_parse(p, "x |> clamp(0, 1)", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(cxpr_expr_ast_call_arg_count(ast) == 3);
    cxpr_expr_ast_free(ast);

    /* `within(...)` is a normal builtin function call. */
    ast = cxpr_expr_ast_parse(p, "within(score, 10, 20)", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_expr_ast_call_name(ast), "within") == 0);
    assert(cxpr_expr_ast_call_arg_count(ast) == 3);
    cxpr_expr_ast_free(ast);

    /* `in [a, b, c]` is set membership: desugars to contains(...). */
    ast = cxpr_expr_ast_parse(p, "score in [10, 20, 30]", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_expr_ast_call_name(ast), "contains") == 0);
    assert(cxpr_expr_ast_call_arg_count(ast) == 2);
    cxpr_expr_ast_free(ast);

    /* `not in [...]` wraps the contains call in a logical NOT. */
    ast = cxpr_expr_ast_parse(p, "score not in [10, 20]", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_UNARY_OP);
    assert(cxpr_expr_ast_operator(ast) == CXPR_TOK_NOT);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "age >= 18 < 65", &err);
    assert(ast);
    char* text = cxpr_expr_ast_to_string(ast);
    assert(text);
    assert(strcmp(text, "age >= 18 and age < 65") == 0);
    free(text);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "18 <= age < 65", &err);
    assert(ast);
    text = cxpr_expr_ast_to_string(ast);
    assert(text);
    assert(strcmp(text, "18 <= age and age < 65") == 0);
    free(text);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "a ? b : c", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_TERNARY);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "2^3^2", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP);
    assert(cxpr_expr_ast_operator(ast) == CXPR_TOK_POWER);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_binary_right(ast)) == CXPR_NODE_BINARY_OP);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "{ x = 0, y: 1, z }", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_RECORD);
    assert(cxpr_expr_ast_record_field_count(ast) == 3);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 0), "x") == 0);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 1), "y") == 0);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 2), "z") == 0);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_record_field_value(ast, 0)) == CXPR_NODE_NUMBER);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_record_field_value(ast, 1)) == CXPR_NODE_NUMBER);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_record_field_value(ast, 2)) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_expr_ast_identifier_name(cxpr_expr_ast_record_field_value(ast, 2)), "z") == 0);
    cxpr_expr_ast_free(ast);

    cxpr_expr_parser_free(p);
}

int main(void) {
    test_parser_expression_forms();
    printf("  \xE2\x9C\x93 parser_expression\n");
    return 0;
}
