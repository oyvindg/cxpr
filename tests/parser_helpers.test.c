#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cxpr_lexer lexer;
    cxpr_token current;
    cxpr_token previous;
    bool had_error;
    cxpr_error last_error;
} parser_test_state;

char* cxpr_parser_token_to_string(const cxpr_token* tok);
cxpr_token cxpr_parser_peek_next(const parser_test_state* p);
cxpr_ast* cxpr_parser_clone_ast(const cxpr_ast* ast);

static void test_parser_helper_functions(void) {
    parser_test_state p;
    cxpr_token tok;
    char* text;
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_ast* clone;

    cxpr_lexer_init(&p.lexer, "alpha + beta");
    p.current = cxpr_lexer_next(&p.lexer);
    tok = cxpr_parser_peek_next(&p);
    assert(tok.type == CXPR_TOK_PLUS);

    text = cxpr_parser_token_to_string(&p.current);
    assert(text);
    assert(strcmp(text, "alpha") == 0);
    free(text);

    assert(parser);
    ast = cxpr_parse(parser, "fn(x=1, y=2)", &err);
    assert(ast);
    clone = cxpr_parser_clone_ast(ast);
    assert(clone);
    assert(cxpr_ast_type(clone) == CXPR_NODE_FUNCTION_CALL);
    assert(cxpr_ast_function_has_named_args(clone));
    cxpr_ast_free(clone);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

int main(void) {
    test_parser_helper_functions();
    printf("  \xE2\x9C\x93 parser_helpers\n");
    return 0;
}
