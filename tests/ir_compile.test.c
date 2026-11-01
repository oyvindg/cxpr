#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

bool cxpr_ir_compile_with_locals(const cxpr_expr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err);
void cxpr_ir_program_reset(cxpr_ir_program* program);

static void test_ir_compile_with_locals_and_fast_kind(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_ir_program program = {0};
    const char* locals[] = {"x"};

    assert(p && reg);
    ast = cxpr_expr_ast_parse(p, "x * 2", &err);
    assert(ast);
    assert(cxpr_ir_compile_with_locals(ast, reg, locals, 1, &program, &err));
    assert(program.count > 0);
    assert(program.fast_result_kind == 1);
    assert(program.code[0].op == CXPR_OP_LOAD_LOCAL || program.code[0].op == CXPR_OP_PUSH_CONST);

    cxpr_ir_program_reset(&program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(p);
}

static void test_ir_compile_array_literal_builds_array_ir(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_ir_program program = {0};

    assert(p && reg);
    ast = cxpr_expr_ast_parse(p, "[1, [2, 3], flag]", &err);
    assert(ast);
    assert(cxpr_ir_compile_with_locals(ast, reg, NULL, 0, &program, &err));
    assert(program.count == 7u);
    assert(program.fast_result_kind == 0);
    assert(program.code[0].op == CXPR_OP_PUSH_CONST);
    assert(program.code[1].op == CXPR_OP_PUSH_CONST);
    assert(program.code[2].op == CXPR_OP_PUSH_CONST);
    assert(program.code[3].op == CXPR_OP_BUILD_ARRAY);
    assert(program.code[3].index == 2u);
    assert(program.code[4].op == CXPR_OP_LOAD_VAR);
    assert(program.code[5].op == CXPR_OP_BUILD_ARRAY);
    assert(program.code[5].index == 3u);
    assert(program.code[6].op == CXPR_OP_RETURN);

    cxpr_ir_program_reset(&program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(p);
}

static void test_ir_compile_rejects_wrong_builtin_arity(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_ir_program program = {0};

    assert(p && reg);
    cxpr_register_defaults(reg);
    ast = cxpr_expr_ast_parse(p, "clamp(e)", &err);
    assert(ast);
    assert(!cxpr_ir_compile_with_locals(ast, reg, NULL, 0, &program, &err));
    assert(err.code == CXPR_ERR_WRONG_ARITY);

    cxpr_ir_program_reset(&program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(p);
}

static void test_long_unknown_field_has_eval_parity(void) {
    const char* source =
        "ccl__cxpr_window_highesaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaatma.cmab";
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ast_ctx = cxpr_context_new();
    cxpr_context* ir_ctx = cxpr_context_new();
    cxpr_error parse_err = {0}, ast_err = {0}, ir_err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, source, &parse_err);
    cxpr_expr_compiled* program;
    cxpr_value ast_value = cxpr_num(0.0), ir_value = cxpr_num(0.0);
    bool ast_ok, ir_ok;

    assert(ast && parse_err.code == CXPR_OK);
    cxpr_register_defaults(reg);
    program = cxpr_expr_compile(ast, reg, &parse_err);
    assert(program);
    ast_ok = cxpr_eval_ast(ast, ast_ctx, reg, &ast_value, &ast_err);
    ir_ok = cxpr_expr_compiled_eval(program, ir_ctx, reg, &ir_value, &ir_err);
    assert(ast_ok == ir_ok);
    assert(ast_ok || ast_err.code == ir_err.code);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ast_ctx);
    cxpr_context_free(ir_ctx);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(p);
}

static void test_known_struct_arithmetic_lowers_selected_fields(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    const char* fields[] = {"x", "y", "z"};
    cxpr_value values[] = {cxpr_num(2.0), cxpr_num(4.0), cxpr_num(8.0)};
    cxpr_struct_value* vector = cxpr_struct_value_new(fields, values, 3u);
    cxpr_value result = {0};
    size_t named_field_loads = 0u;

    assert(p && reg && ctx && vector);
    ast = cxpr_expr_ast_parse(
        p, "(vector * 2).x + (vector * 2).y + (vector * 2).z", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_ir_result_kind(program) == CXPR_IR_RESULT_NUMBER);
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_GET_FIELD);
        assert(instruction.op != CXPR_IR_OP_CALL_AST);
        named_field_loads += instruction.op == CXPR_IR_OP_LOAD_NAMED_FIELD;
    }
    assert(named_field_loads == 3u);

    cxpr_context_set_struct(ctx, "vector", vector);
    assert(cxpr_expr_compiled_eval(program, ctx, reg, &result, &err));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 28.0);

    cxpr_value_free(&result);
    cxpr_struct_value_free(vector);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_expr_parser_free(p);
}

static void test_deep_scalar_defined_chain_inlines_without_call_opcode(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    double result = NAN;

    assert(parser && registry && context);
    cxpr_register_defaults(registry);
    assert(cxpr_registry_define_fn(registry, "q0(x) => x * x").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q1(x) => sqrt(q0(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q2(x) => sqrt(q1(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q3(x) => sqrt(q2(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q4(x) => sqrt(q3(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q5(x) => sqrt(q4(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q6(x) => sqrt(q5(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q7(x) => sqrt(q6(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q8(x) => sqrt(q7(x))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(registry, "q9(x) => sqrt(q8(x))").code == CXPR_OK);

    ast = cxpr_expr_ast_parse(parser, "q9(x)", &error);
    assert(ast);
    program = cxpr_expr_compile(ast, registry, &error);
    assert(program);
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_CALL_DEFINED);
    }
    cxpr_context_set(context, "x", 4.0);
    assert(cxpr_expr_compiled_eval_number(program, context, registry, &result, &error));
    assert(isfinite(result));

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_ir_compile_with_locals_and_fast_kind();
    test_ir_compile_array_literal_builds_array_ir();
    test_ir_compile_rejects_wrong_builtin_arity();
    test_long_unknown_field_has_eval_parity();
    test_known_struct_arithmetic_lowers_selected_fields();
    test_deep_scalar_defined_chain_inlines_without_call_opcode();
    printf("  \xE2\x9C\x93 ir_compile\n");
    return 0;
}
