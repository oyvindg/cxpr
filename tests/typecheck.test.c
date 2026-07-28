#include <assert.h>
#include <cxpr/cxpr.h>
#include <stdlib.h>
#include <string.h>

static cxpr_expr_ast* parse_expr(const char* expr) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, expr, &err);
    cxpr_parser_free(parser);
    assert(ast);
    return ast;
}

static cxpr_value atr_func(const cxpr_value* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    (void)userdata;
    return cxpr_num(1.0);
}

static void expect_reject(cxpr_registry* reg, const char* expr) {
    cxpr_error err = {0};
    cxpr_expr_ast* ast = parse_expr(expr);
    assert(!cxpr_typecheck(ast, reg, NULL, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    assert(err.message && strstr(err.message, "type error"));
    cxpr_expr_ast_free(ast);
}

static void expect_bool_root_reject(cxpr_registry* reg, const char* expr) {
    cxpr_error err = {0};
    cxpr_expr_ast* ast = parse_expr(expr);
    assert(!cxpr_typecheck_bool_root(ast, reg, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    cxpr_expr_ast_free(ast);
}

static void expect_accept(cxpr_registry* reg, const char* expr) {
    cxpr_error err = {0};
    cxpr_expr_ast* ast = parse_expr(expr);
    assert(cxpr_typecheck(ast, reg, NULL, &err));
    assert(err.code == CXPR_OK);
    cxpr_expr_ast_free(ast);
}

static void expect_backend_rejects(cxpr_registry* reg, const char* expr) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = parse_expr(expr);
    cxpr_value value = {0};
    cxpr_program* program = NULL;
    char* generated = NULL;

    assert(ctx);
    cxpr_context_set(ctx, "x", 5.0);

    assert(!cxpr_eval_ast(ast, ctx, reg, &value, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    err = (cxpr_error){0};
    program = cxpr_compile(ast, reg, &err);
    assert(!program);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    err = (cxpr_error){0};
    generated = cxpr_expr_ast_to_c(ast, NULL, &err);
    assert(!generated);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    free(generated);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
}

static cxpr_registry* make_registry(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_value_type arg = CXPR_VALUE_NUMBER;
    assert(reg);
    cxpr_register_basket_builtins(reg);
    cxpr_registry_add_typed(reg, "atr", atr_func, 1, 1, &arg,
                            CXPR_VALUE_NUMBER, NULL, NULL);
    return reg;
}

int main(void) {
    cxpr_registry* reg = make_registry();

    expect_reject(reg, "not 1");
    expect_reject(reg, "1 and 2");
    expect_bool_root_reject(reg, "close ? 1 : 0");
    expect_reject(reg, "any(atr(14))");
    expect_reject(reg, "(close + 1) and x");
    expect_reject(reg, "cond ? 1 : true");
    expect_backend_rejects(reg, "1 and 2");

    expect_accept(reg, "close > 5 and rsi < 30");
    expect_accept(reg, "not (x > 5)");
    expect_accept(reg, "any(x > 0)");
    expect_accept(reg, "$flag and y");
    expect_accept(reg, "ind() ? a : b");

    cxpr_registry_free(reg);
    return 0;
}
