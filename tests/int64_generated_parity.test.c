#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "int64_generated.h"

typedef cxpr_c_typed_checked_result (*generated_fn)(void);

static void assert_parity(const char* source, generated_fn generated) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error ast_error = {0};
    cxpr_error ir_error = {0};
    cxpr_value ast_value = cxpr_null();
    cxpr_value ir_value = cxpr_null();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &ast_error);
    cxpr_expr_compiled* program = ast ? cxpr_expr_compile(ast, registry, &ir_error) : NULL;
    cxpr_c_typed_checked_result c_value;

    assert(ast && program);
    (void)cxpr_eval_ast(ast, context, registry, &ast_value, &ast_error);
    (void)cxpr_expr_compiled_eval(program, context, registry, &ir_value, &ir_error);
    c_value = generated();
    assert(ast_error.code == ir_error.code);
    assert(ast_error.code == c_value.error);
    if (ast_error.code == CXPR_OK) {
        assert(ast_value.type == ir_value.type);
        assert(ast_value.type == c_value.value.type);
        if (ast_value.type == CXPR_VALUE_INT64)
            assert(ast_value.i64 == ir_value.i64 && ast_value.i64 == c_value.value.i64);
        else if (ast_value.type == CXPR_VALUE_NUMBER)
            assert(ast_value.d == ir_value.d && ast_value.d == c_value.value.d);
        else if (ast_value.type == CXPR_VALUE_BOOL)
            assert(ast_value.b == ir_value.b && ast_value.b == c_value.value.b);
    }

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    assert_parity("9007199254740993i64 + 2i64", gen_int_add);
    assert_parity("5i64 / 2i64", gen_int_div);
    assert_parity("5i64 + 0.5", gen_mixed_add);
    assert_parity("5i64 == 5.0", gen_mixed_equal);
    assert_parity("9007199254740993i64 > 9007199254740992.0", gen_boundary_gt);
    assert_parity("9223372036854775807i64 + 1i64", gen_overflow);
    puts("  \xE2\x9C\x93 int64 generated C parity");
    return 0;
}
