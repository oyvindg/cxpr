#include <assert.h>
#include <math.h>
#include <stdio.h>

double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);
cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              const double* locals,
                              size_t local_count,
                              cxpr_error* err);

static void test_ir_exec_fast_scalar_path(void) {
    cxpr_ir_instr code[] = {
        {.op = CXPR_OP_PUSH_CONST, .value = 2.0},
        {.op = CXPR_OP_PUSH_CONST, .value = 3.0},
        {.op = CXPR_OP_ADD},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program program = {.code = code, .count = 4};
    cxpr_error err = {0};
    double out = cxpr_ir_exec_scalar_fast(&program, NULL, NULL, NULL, 0, &err);

    assert(err.code == CXPR_OK);
    assert(out == 5.0);
}

static void test_array_index_dispatches_away_from_scalar_fast_path(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast;
    cxpr_ir_program program = {0};
    assert(parser && registry && context);
    cxpr_context_set(context, "i", 1.0);

    ast = cxpr_expr_ast_parse(parser, "[10, 20][i] + 1", &error);
    assert(ast && error.code == CXPR_OK);
    assert(cxpr_ir_compile(ast, registry, &program, &error));
    assert(program.fast_result_kind == CXPR_IR_RESULT_UNKNOWN);
    assert(cxpr_ir_exec(&program, context, registry, &error) == 21.0);
    assert(error.code == CXPR_OK);
    cxpr_ir_program_reset(&program);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser, "[true, false][i]", &error);
    assert(ast && error.code == CXPR_OK);
    assert(cxpr_ir_compile(ast, registry, &program, &error));
    assert(program.fast_result_kind == CXPR_IR_RESULT_UNKNOWN);
    {
        cxpr_value value = cxpr_ir_exec_typed(
            &program, context, registry, NULL, 0u, &error);
        assert(error.code == CXPR_OK);
        assert(value.type == CXPR_VALUE_BOOL && !value.b);
        cxpr_value_free(&value);
    }

    cxpr_ir_program_reset(&program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_ir_exec_fast_scalar_path();
    test_array_index_dispatches_away_from_scalar_fast_path();
    printf("  \xE2\x9C\x93 ir_exec_fast\n");
    return 0;
}
