#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

bool cxpr_eval_constant_double(const cxpr_ast* ast, double* out);
cxpr_ast* cxpr_eval_clone_ast(const cxpr_ast* ast);
bool cxpr_eval_ast_contains_string_literal(const cxpr_ast* ast);

static void test_eval_helper_functions(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_ast* clone;
    double out = 0.0;

    assert(p);
    ast = cxpr_parse(p, "2 + 3 * 4", &err);
    assert(ast);
    assert(cxpr_eval_constant_double(ast, &out));
    assert(out == 14.0);

    clone = cxpr_eval_clone_ast(ast);
    assert(clone);
    assert(cxpr_ast_type(clone) == cxpr_ast_type(ast));
    cxpr_ast_free(clone);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "fn(\"1h\")", &err);
    assert(ast);
    assert(cxpr_eval_ast_contains_string_literal(ast));
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
}

int main(void) {
    test_eval_helper_functions();
    printf("  \xE2\x9C\x93 eval_helpers\n");
    return 0;
}
