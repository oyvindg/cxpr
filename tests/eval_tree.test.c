#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_eval_tree_access_paths(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    double out = 0.0;
    bool bout = false;
    const char* fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};

    assert(p && ctx && reg);
    cxpr_context_set_fields(ctx, "pose", fields, values, 2);
    cxpr_context_set(ctx, "legacy.value", 7.0);

    ast = cxpr_parse(p, "pose.x + pose.y", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 7.0);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "legacy.value == 7", &err);
    assert(ast);
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &bout, &err));
    assert(bout);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

int main(void) {
    test_eval_tree_access_paths();
    printf("  \xE2\x9C\x93 eval_tree\n");
    return 0;
}
