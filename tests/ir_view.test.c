/**
 * @file ir_view.test.c
 * @brief Public API tests for the cxpr IR view.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_ir_view_null_inputs(void) {
    cxpr_ir_instruction instr = { .op = CXPR_IR_OP_RETURN };

    assert(cxpr_expr_compiled_ir_count(NULL) == 0);
    assert(cxpr_expr_compiled_ir_result_kind(NULL) == CXPR_IR_RESULT_UNKNOWN);
    assert(cxpr_expr_compiled_ir_instruction(NULL, 0, &instr) == false);
    assert(instr.op == CXPR_IR_OP_UNKNOWN);
    assert(cxpr_expr_compiled_ir_instruction(NULL, 0, NULL) == false);
    assert(strcmp(cxpr_ir_opcode_name(CXPR_IR_OP_ADD), "ADD") == 0);
    assert(strcmp(cxpr_ir_opcode_name((cxpr_ir_opcode)999), "UNKNOWN") == 0);

    printf("  ok test_ir_view_null_inputs\n");
}

static void test_ir_view_compiled_expression(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "close + $offset > signal", &err);
    assert(ast);

    cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &err);
    assert(program);
    assert(err.code == CXPR_OK);

    const size_t count = cxpr_expr_compiled_ir_count(program);
    assert(count > 0);
    assert(cxpr_expr_compiled_ir_result_kind(program) == CXPR_IR_RESULT_BOOL);

    bool saw_close = false;
    bool saw_offset = false;
    bool saw_signal = false;
    bool saw_add = false;
    bool saw_cmp_gt = false;
    bool saw_return = false;

    for (size_t i = 0; i < count; ++i) {
        cxpr_ir_instruction instr;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instr));
        assert(instr.op != CXPR_IR_OP_UNKNOWN);

        if (instr.op == CXPR_IR_OP_LOAD_VAR && instr.name &&
            strcmp(instr.name, "close") == 0) {
            assert(instr.has_hash);
            saw_close = true;
        } else if (instr.op == CXPR_IR_OP_LOAD_PARAM && instr.name &&
                   strcmp(instr.name, "offset") == 0) {
            assert(instr.has_hash);
            saw_offset = true;
        } else if (instr.op == CXPR_IR_OP_LOAD_VAR && instr.name &&
                   strcmp(instr.name, "signal") == 0) {
            assert(instr.has_hash);
            saw_signal = true;
        } else if (instr.op == CXPR_IR_OP_ADD) {
            saw_add = true;
        } else if (instr.op == CXPR_IR_OP_CMP_GT) {
            saw_cmp_gt = true;
        } else if (instr.op == CXPR_IR_OP_RETURN) {
            saw_return = true;
        }
    }

    assert(saw_close);
    assert(saw_offset);
    assert(saw_signal);
    assert(saw_add);
    assert(saw_cmp_gt);
    assert(saw_return);

    cxpr_ir_instruction out_of_range = { .op = CXPR_IR_OP_RETURN };
    assert(cxpr_expr_compiled_ir_instruction(program, count, &out_of_range) == false);
    assert(out_of_range.op == CXPR_IR_OP_UNKNOWN);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);

    printf("  ok test_ir_view_compiled_expression\n");
}

static void test_ir_view_lookback_uses_push_pop_opcodes(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "close[1]", &err);
    cxpr_expr_compiled* program;
    cxpr_ir_instruction instr = {0};

    assert(ast);
    program = cxpr_expr_compile(ast, registry, &err);
    assert(program);
    assert(err.code == CXPR_OK);
    assert(cxpr_expr_compiled_ir_count(program) >= 4u);
    assert(cxpr_expr_compiled_ir_instruction(program, 0u, &instr));
    assert(instr.op == CXPR_IR_OP_LOOKBACK_PUSH);
    assert(instr.has_index);
    assert(instr.index == 1u);
    assert(cxpr_expr_compiled_ir_instruction(program, 1u, &instr));
    assert(instr.op == CXPR_IR_OP_LOAD_VAR);
    assert(cxpr_expr_compiled_ir_instruction(program, 2u, &instr));
    assert(instr.op == CXPR_IR_OP_LOOKBACK_POP);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser, "close[1][2]", &err);
    assert(ast);
    program = cxpr_expr_compile(ast, registry, &err);
    assert(program);
    assert(cxpr_expr_compiled_ir_instruction(program, 0u, &instr));
    assert(instr.op == CXPR_IR_OP_LOOKBACK_PUSH);
    assert(instr.has_index);
    assert(instr.index == 3u);
    assert(cxpr_expr_compiled_ir_instruction(program, 1u, &instr));
    assert(instr.op == CXPR_IR_OP_LOAD_VAR);
    assert(cxpr_expr_compiled_ir_instruction(program, 2u, &instr));
    assert(instr.op == CXPR_IR_OP_LOOKBACK_POP);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);

    printf("  ok test_ir_view_lookback_uses_push_pop_opcodes\n");
}

static void test_ir_view_array_instruction(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "[1, 2, 3]", &err);
    cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &err);
    bool found = false;

    assert(program);
    for (size_t i = 0u; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instr;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instr));
        if (instr.op == CXPR_IR_OP_BUILD_ARRAY) {
            assert(instr.has_arg_count);
            assert(instr.arg_count == 3u);
            assert(strcmp(cxpr_ir_opcode_name(instr.op), "BUILD_ARRAY") == 0);
            found = true;
        }
    }
    assert(found);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    printf("  ok test_ir_view_array_instruction\n");
}

int main(void) {
    test_ir_view_null_inputs();
    test_ir_view_compiled_expression();
    test_ir_view_lookback_uses_push_pop_opcodes();
    test_ir_view_array_instruction();
    printf("ir_view tests passed\n");
    return 0;
}
