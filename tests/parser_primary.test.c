#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parser_primary_forms(void) {
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_parser* p = cxpr_parser_new();

    assert(p);

    ast = cxpr_parse(p, "macd(fast=9, slow=21).signal", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_ast_producer_name(ast), "macd") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(ast, 0), "fast") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(ast, 1), "slow") == 0);
    assert(strcmp(cxpr_ast_producer_field(ast), "signal") == 0);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    p = cxpr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "body.velocity.x[2]", &err);
    assert(ast);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_CHAIN_ACCESS);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);

    p = cxpr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "\"tf_1h\"", &err);
    assert(ast == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strcmp(err.message, "String literals not yet supported") == 0);
    cxpr_parser_free(p);

    p = cxpr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_parse(p, "foo = 1", &err);
    assert(ast == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strcmp(err.message, "Unexpected token after expression") == 0);
    cxpr_parser_free(p);
}

int main(void) {
    test_parser_primary_forms();
    printf("  \xE2\x9C\x93 parser_primary\n");
    return 0;
}
