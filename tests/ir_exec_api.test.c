#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err);
double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);
cxpr_value cxpr_ir_runtime_error(cxpr_error* err, const char* message);

static void test_ir_exec_api_wrappers(void) {
    cxpr_ir_instr code[] = {
        {.op = CXPR_OP_LOAD_LOCAL, .index = 0},
        {.op = CXPR_OP_PUSH_CONST, .value = 2.0},
        {.op = CXPR_OP_MUL},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program program = {.code = code, .count = 4, .fast_result_kind = 1};
    cxpr_error err = {0};
    double local = 4.0;
    double out = cxpr_ir_exec_with_locals(&program, NULL, NULL, &local, 1, &err);

    assert(err.code == CXPR_OK);
    assert(out == 8.0);

    code[0].op = CXPR_OP_PUSH_CONST;
    code[0].value = 6.0;
    out = cxpr_ir_exec(&program, NULL, NULL, &err);
    assert(err.code == CXPR_OK);
    assert(out == 12.0);
}

static void test_ir_runtime_error_helper(void) {
    cxpr_error err = {0};
    cxpr_value value = cxpr_ir_runtime_error(&err, "runtime helper test");

    assert(value.type == CXPR_VALUE_NUMBER);
    assert(isnan(value.d));
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
}

static void test_standalone_bindings_rebind_after_context_growth(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "x * 2 + $offset", &error);
    cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &error);
    double result = NAN;
    char name[32];

    assert(parser && registry && context && ast && program);
    cxpr_context_set(context, "x", 3.0);
    cxpr_context_set_param(context, "offset", 1.0);
    assert(cxpr_expr_compiled_eval_number(program, context, registry, &result, &error));
    assert(result == 7.0);

    for (size_t i = 0u; i < 128u; ++i) {
        snprintf(name, sizeof(name), "growth_%zu", i);
        cxpr_context_set(context, name, (double)i);
    }
    cxpr_context_set(context, "x", 5.0);
    cxpr_context_set_param(context, "offset", 4.0);
    assert(cxpr_expr_compiled_eval_number(program, context, registry, &result, &error));
    assert(result == 14.0);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

static void test_standalone_struct_field_binding_rebinds_on_replacement(void) {
    const char* fields[] = {"x", "y"};
    cxpr_value first_values[] = {cxpr_num(2.0), cxpr_num(3.0)};
    cxpr_value second_values[] = {cxpr_num(5.0), cxpr_num(7.0)};
    cxpr_struct_value* first = cxpr_struct_value_new(fields, first_values, 2u);
    cxpr_struct_value* second = cxpr_struct_value_new(fields, second_values, 2u);
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "(vector * 2).x", &error);
    cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &error);
    double result = NAN;

    assert(first && second && parser && registry && context && ast && program);
    cxpr_context_set_struct(context, "vector", first);
    assert(cxpr_expr_compiled_eval_number(program, context, registry, &result, &error));
    assert(result == 4.0);
    cxpr_context_set_struct(context, "vector", second);
    assert(cxpr_expr_compiled_eval_number(program, context, registry, &result, &error));
    assert(result == 10.0);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    cxpr_struct_value_free(second);
    cxpr_struct_value_free(first);
}

int main(void) {
    test_ir_exec_api_wrappers();
    test_ir_runtime_error_helper();
    test_standalone_bindings_rebind_after_context_growth();
    test_standalone_struct_field_binding_rebinds_on_replacement();
    printf("  \xE2\x9C\x93 ir_exec_api\n");
    return 0;
}
