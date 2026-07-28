#include <cxpr/cxpr.h>

#include <math.h>

int main(void) {
    cxpr_error err = {0};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "1 + 2", &err);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    double result = NAN;
    int failed = !ast || !ctx || !reg ||
                 !cxpr_eval_ast_number(ast, ctx, reg, &result, &err) ||
                 result != 3.0;

    cxpr_expr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    return failed;
}
