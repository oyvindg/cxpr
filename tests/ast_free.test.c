#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void test_ast_free_handles_nested_trees(void) {
    cxpr_ast** args = (cxpr_ast**)calloc(1, sizeof(*args));
    cxpr_ast* ast;

    assert(args);
    args[0] = cxpr_ast_new_number(9.0);
    ast = cxpr_ast_new_ternary(
        cxpr_ast_new_binary_op(CXPR_TOK_GT,
                               cxpr_ast_new_identifier("x"),
                               cxpr_ast_new_number(0.0)),
        cxpr_ast_new_function_call("sqrt", args, 1),
        cxpr_ast_new_lookback(cxpr_ast_new_identifier("close"),
                              cxpr_ast_new_number(1.0)));

    assert(ast);
    cxpr_ast_free(ast);
    cxpr_ast_free(NULL);
}

int main(void) {
    test_ast_free_handles_nested_trees();
    printf("  \xE2\x9C\x93 ast_free\n");
    return 0;
}
