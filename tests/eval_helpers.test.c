#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

bool cxpr_eval_constant_double(const cxpr_expr_ast* ast, double* out);
cxpr_expr_ast* cxpr_eval_clone_ast(const cxpr_expr_ast* ast);
bool cxpr_eval_ast_contains_string_literal(const cxpr_expr_ast* ast);
void cxpr_eval_memo_enter(cxpr_context* ctx);
void cxpr_eval_memo_clear(cxpr_context* ctx);
void cxpr_eval_memo_leave(cxpr_context* ctx);

static void test_eval_helper_functions(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_ast* clone;
    double out = 0.0;

    assert(p);
    ast = cxpr_expr_ast_parse(p, "2 + 3 * 4", &err);
    assert(ast);
    assert(cxpr_eval_constant_double(ast, &out));
    assert(out == 14.0);

    clone = cxpr_eval_clone_ast(ast);
    assert(clone);
    assert(cxpr_expr_ast_kind_of(clone) == cxpr_expr_ast_kind_of(ast));
    cxpr_expr_ast_free(clone);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "fn(\"1h\")", &err);
    assert(ast);
    assert(cxpr_eval_ast_contains_string_literal(ast));
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);
}

static void test_eval_memo_clear_accepts_context_and_null(void) {
    cxpr_context* ctx = cxpr_context_new();

    assert(ctx);
    cxpr_eval_memo_enter(ctx);
    cxpr_eval_memo_clear(ctx);
    cxpr_eval_memo_leave(ctx);
    cxpr_eval_memo_clear(NULL);
    cxpr_context_free(ctx);
}

int main(void) {
    test_eval_helper_functions();
    test_eval_memo_clear_accepts_context_and_null();
    printf("  \xE2\x9C\x93 eval_helpers\n");
    return 0;
}
