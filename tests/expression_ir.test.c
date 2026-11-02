/**
 * @file expression_ir.test.c
 * @brief Internal tests for expression-evaluator compiled-program integration.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static void test_formula_compile_creates_programs(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "base", "x + 1", &err) == true);
    assert(cxpr_expression_add(evaluator, "signal", "base * 2", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == true);
    assert(err.code == CXPR_OK);

    assert(evaluator->expressions[0].program != NULL);
    assert(evaluator->expressions[1].program != NULL);

    cxpr_context_set(ctx, "x", 3.0);
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found = false;
    ASSERT_DOUBLE_EQ(cxpr_expression_get_double(evaluator, "signal", &found), 8.0);
    assert(found == true);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_compile_creates_programs\n");
}

static void test_formula_compile_unknown_function_fails(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "signal", "missing_fn(x) + 1", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == false);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    assert(strcmp(err.message, "Expression 'signal': Unknown function 'missing_fn'") == 0);
    assert(evaluator->compiled == false);
    assert(evaluator->expressions[0].program == NULL);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_compile_unknown_function_fails\n");
}

static void test_static_map_reducer_compiles_to_ir(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    double result = 0.0;
    assert(reg && parser && ctx);
    cxpr_register_defaults(reg);
    ast = cxpr_expr_ast_parse(parser, "sum(map([1, 2, 3], x => x * 2))", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_eval_number(program, ctx, reg, &result, &err));
    ASSERT_DOUBLE_EQ(result, 12.0);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

static void test_static_reduce_compiles_to_ir(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    double result = 0.0;
    assert(reg && parser && ctx);
    cxpr_register_defaults(reg);
    ast = cxpr_expr_ast_parse(parser,
        "reduce(map([1, 2, 3], x => x * 2), 0, step => step.acc + step.value + step.index)",
        &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_eval_number(program, ctx, reg, &result, &err));
    ASSERT_DOUBLE_EQ(result, 15.0);
    bool saw_load_temp = false;
    bool saw_store_temp = false;
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_CALL_AST);
        saw_load_temp |= instruction.op == CXPR_IR_OP_LOAD_TEMP;
        saw_store_temp |= instruction.op == CXPR_IR_OP_STORE_TEMP;
    }
    assert(saw_load_temp && saw_store_temp);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser,
        "reduce([{ signal = 2, gain = 0.25 }, { signal = 4, gain = 0.5 }], "
        "0, step => step.acc + step.value.signal * step.value.gain)", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_eval_number(program, ctx, reg, &result, &err));
    ASSERT_DOUBLE_EQ(result, 2.5);
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_CALL_AST);
        assert(instruction.op != CXPR_IR_OP_LOAD_CHAIN);
        assert(instruction.op != CXPR_IR_OP_LOAD_TEMP);
        assert(instruction.op != CXPR_IR_OP_STORE_TEMP);
    }
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser,
        "reduce(map([{ signal = 2, gain = 0.25 }, { signal = 4, gain = 0.5 }], "
        "item => item.signal * item.gain), 0, step => step.acc + step.value)", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_eval_number(program, ctx, reg, &result, &err));
    ASSERT_DOUBLE_EQ(result, 2.5);
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_CALL_AST);
        assert(instruction.op != CXPR_IR_OP_GET_FIELD);
        assert(instruction.op != CXPR_IR_OP_LOAD_TEMP);
        assert(instruction.op != CXPR_IR_OP_STORE_TEMP);
    }
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser,
        "reduce([], 42, step => step.acc + step.value)", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program);
    assert(cxpr_expr_compiled_eval_number(program, ctx, reg, &result, &err));
    ASSERT_DOUBLE_EQ(result, 42.0);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

static void test_dotted_expression_struct_field_dependency_ir(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    const char* fields[] = {"line", "signal"};
    cxpr_value values[] = {cxpr_num(3.0), cxpr_num(2.0)};
    cxpr_struct_value* point = cxpr_struct_value_new(fields, values, 2u);
    cxpr_error err = {0};
    bool found = false;

    assert(point != NULL);
    cxpr_context_set_struct(ctx, "raw", point);
    assert(cxpr_expression_add(evaluator, "scope.m", "raw", &err) == true);
    assert(cxpr_expression_add(evaluator, "entry", "scope.m.line > scope.m.signal", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == true);
    assert(err.code == CXPR_OK);

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) == true);
    assert(found == true);

    cxpr_struct_value_free(point);
    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_dotted_expression_struct_field_dependency_ir\n");
}

static void test_dotted_expression_scope_requires_struct_prefix_ir(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    assert(cxpr_expression_add(evaluator, "scope.m", "1", &err) == true);
    assert(cxpr_expression_add(evaluator, "entry", "scope.m.line > 0", &err) == true);
    assert(cxpr_evaluator_compile(evaluator, &err) == true);
    assert(err.code == CXPR_OK);

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strstr(err.message, "Expression-scope chain has no struct prefix") != NULL);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    printf("  ✓ test_dotted_expression_scope_requires_struct_prefix_ir\n");
}

int main(void) {
    printf("Running expression IR tests...\n");
    test_formula_compile_creates_programs();
    test_formula_compile_unknown_function_fails();
    test_static_map_reducer_compiles_to_ir();
    test_static_reduce_compiles_to_ir();
    test_dotted_expression_struct_field_dependency_ir();
    test_dotted_expression_scope_requires_struct_prefix_ir();
    printf("All expression IR tests passed!\n");
    return 0;
}
