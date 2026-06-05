/**
 * @file ir_view.test.c
 * @brief Public API tests for the cxpr IR view.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_ir_view_null_inputs(void) {
    cxpr_ir_view_instr instr = { .op = CXPR_IR_VIEW_OP_RETURN };

    assert(cxpr_ir_view_count(NULL) == 0);
    assert(cxpr_ir_view_program_result_kind(NULL) == CXPR_IR_VIEW_RESULT_UNKNOWN);
    assert(cxpr_ir_view_instr_at(NULL, 0, &instr) == false);
    assert(instr.op == CXPR_IR_VIEW_OP_UNKNOWN);
    assert(cxpr_ir_view_instr_at(NULL, 0, NULL) == false);
    assert(strcmp(cxpr_ir_view_opcode_name(CXPR_IR_VIEW_OP_ADD), "ADD") == 0);
    assert(strcmp(cxpr_ir_view_opcode_name((cxpr_ir_view_opcode)999), "UNKNOWN") == 0);

    printf("  ok test_ir_view_null_inputs\n");
}

static void test_ir_view_compiled_expression(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "close + $offset > signal", &err);
    assert(ast);

    cxpr_program* program = cxpr_compile(ast, registry, &err);
    assert(program);
    assert(err.code == CXPR_OK);

    const size_t count = cxpr_ir_view_count(program);
    assert(count > 0);
    assert(cxpr_ir_view_program_result_kind(program) == CXPR_IR_VIEW_RESULT_BOOL);

    bool saw_close = false;
    bool saw_offset = false;
    bool saw_signal = false;
    bool saw_add = false;
    bool saw_cmp_gt = false;
    bool saw_return = false;

    for (size_t i = 0; i < count; ++i) {
        cxpr_ir_view_instr instr;
        assert(cxpr_ir_view_instr_at(program, i, &instr));
        assert(instr.op != CXPR_IR_VIEW_OP_UNKNOWN);

        if (instr.op == CXPR_IR_VIEW_OP_LOAD_VAR && instr.name &&
            strcmp(instr.name, "close") == 0) {
            assert(instr.has_hash);
            saw_close = true;
        } else if (instr.op == CXPR_IR_VIEW_OP_LOAD_PARAM && instr.name &&
                   strcmp(instr.name, "offset") == 0) {
            assert(instr.has_hash);
            saw_offset = true;
        } else if (instr.op == CXPR_IR_VIEW_OP_LOAD_VAR && instr.name &&
                   strcmp(instr.name, "signal") == 0) {
            assert(instr.has_hash);
            saw_signal = true;
        } else if (instr.op == CXPR_IR_VIEW_OP_ADD) {
            saw_add = true;
        } else if (instr.op == CXPR_IR_VIEW_OP_CMP_GT) {
            saw_cmp_gt = true;
        } else if (instr.op == CXPR_IR_VIEW_OP_RETURN) {
            saw_return = true;
        }
    }

    assert(saw_close);
    assert(saw_offset);
    assert(saw_signal);
    assert(saw_add);
    assert(saw_cmp_gt);
    assert(saw_return);

    cxpr_ir_view_instr out_of_range = { .op = CXPR_IR_VIEW_OP_RETURN };
    assert(cxpr_ir_view_instr_at(program, count, &out_of_range) == false);
    assert(out_of_range.op == CXPR_IR_VIEW_OP_UNKNOWN);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_parser_free(parser);

    printf("  ok test_ir_view_compiled_expression\n");
}

int main(void) {
    test_ir_view_null_inputs();
    test_ir_view_compiled_expression();
    printf("ir_view tests passed\n");
    return 0;
}
