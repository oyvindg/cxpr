#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void test_ast_free_handles_nested_trees(void) {
    cxpr_expr_ast** args = (cxpr_expr_ast**)calloc(1, sizeof(*args));
    cxpr_expr_ast* ast;

    assert(args);
    args[0] = cxpr_expr_ast_number_new(9.0);
    ast = cxpr_expr_ast_ternary_new(
        cxpr_expr_ast_binary_new(CXPR_TOK_GT,
                               cxpr_expr_ast_identifier_new("x"),
                               cxpr_expr_ast_number_new(0.0)),
        cxpr_expr_ast_call_new("sqrt", args, 1),
        cxpr_expr_ast_lookback_new(cxpr_expr_ast_identifier_new("close"),
                              cxpr_expr_ast_number_new(1.0)));

    assert(ast);
    cxpr_expr_ast_free(ast);
    cxpr_expr_ast_free(NULL);
}

int main(void) {
    test_ast_free_handles_nested_trees();
    printf("  \xE2\x9C\x93 ast_free\n");
    return 0;
}
