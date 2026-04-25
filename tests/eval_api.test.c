#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_eval_api_wrappers(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_value value = {0};
    double number = 0.0;
    bool boolean = false;

    assert(p && ctx && reg);
    cxpr_context_set(ctx, "x", 5.0);
    ast = cxpr_parse(p, "x > 3 ? 1 : 0", &err);
    assert(ast);

    assert(cxpr_eval_ast(ast, ctx, reg, &value, &err));
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 1.0);

    assert(cxpr_eval_ast_number(ast, ctx, reg, &number, &err));
    assert(number == 1.0);

    cxpr_ast_free(ast);
    ast = cxpr_parse(p, "x > 3", &err);
    assert(ast);
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &boolean, &err));
    assert(boolean);

    assert(!cxpr_eval_ast(ast, ctx, reg, NULL, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

int main(void) {
    test_eval_api_wrappers();
    printf("  \xE2\x9C\x93 eval_api\n");
    return 0;
}
