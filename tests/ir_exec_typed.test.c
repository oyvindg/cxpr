#include <assert.h>
#include <stdio.h>
#include <string.h>

cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                              const cxpr_registry* reg, const double* locals,
                              size_t local_count, cxpr_error* err);

static void test_ir_exec_typed_bool_path(void) {
    cxpr_ir_instr code[] = {
        {.op = CXPR_OP_PUSH_BOOL, .value = 1.0},
        {.op = CXPR_OP_NOT},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program program = {.code = code, .count = 3};
    cxpr_error err = {0};
    cxpr_value out = cxpr_ir_exec_typed(&program, NULL, NULL, NULL, 0, &err);

    assert(err.code == CXPR_OK);
    assert(out.type == CXPR_VALUE_BOOL);
    assert(out.b == false);
}

static void test_ir_exec_typed_numeric_truthiness(void) {
    {
        cxpr_ir_instr code[] = {
            {.op = CXPR_OP_PUSH_CONST, .value = 0.0},
            {.op = CXPR_OP_NOT},
            {.op = CXPR_OP_RETURN}
        };
        cxpr_ir_program program = {.code = code, .count = 3};
        cxpr_error err = {0};
        cxpr_value out = cxpr_ir_exec_typed(&program, NULL, NULL, NULL, 0, &err);
        assert(err.code == CXPR_ERR_TYPE_MISMATCH);
        assert(out.type == CXPR_VALUE_NUMBER);
    }
    {
        cxpr_ir_instr code[] = {
            {.op = CXPR_OP_PUSH_CONST, .value = 2.0},
            {.op = CXPR_OP_JUMP_IF_FALSE, .index = 4},
            {.op = CXPR_OP_PUSH_BOOL, .value = 1.0},
            {.op = CXPR_OP_RETURN},
            {.op = CXPR_OP_PUSH_BOOL, .value = 0.0},
            {.op = CXPR_OP_RETURN}
        };
        cxpr_ir_program program = {.code = code, .count = 6};
        cxpr_error err = {0};
        cxpr_value out = cxpr_ir_exec_typed(&program, NULL, NULL, NULL, 0, &err);
        assert(err.code == CXPR_ERR_TYPE_MISMATCH);
        assert(out.type == CXPR_VALUE_NUMBER);
    }
}

static void test_ir_exec_typed_string_literal(void) {
    cxpr_ir_instr code[] = {
        {.op = CXPR_OP_PUSH_STRING, .name = "1h"},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program program = {.code = code, .count = 2};
    cxpr_error err = {0};
    cxpr_value out = cxpr_ir_exec_typed(&program, NULL, NULL, NULL, 0, &err);

    assert(err.code == CXPR_OK);
    assert(out.type == CXPR_VALUE_STRING);
    assert(strcmp(out.str, "1h") == 0);
}

static void test_ir_exec_typed_lookback_brackets_are_stack_neutral(void) {
    cxpr_ir_instr code[] = {
        {.op = CXPR_OP_LOOKBACK_PUSH, .index = 2},
        {.op = CXPR_OP_LOOKBACK_PUSH, .index = 3},
        {.op = CXPR_OP_LOOKBACK_POP},
        {.op = CXPR_OP_LOOKBACK_POP},
        {.op = CXPR_OP_PUSH_CONST, .value = 7.0},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program program = {.code = code, .count = 6};
    cxpr_error err = {0};
    cxpr_value out = cxpr_ir_exec_typed(&program, NULL, NULL, NULL, 0, &err);

    assert(err.code == CXPR_OK);
    assert(out.type == CXPR_VALUE_NUMBER);
    assert(out.d == 7.0);
}

int main(void) {
    test_ir_exec_typed_bool_path();
    test_ir_exec_typed_numeric_truthiness();
    test_ir_exec_typed_string_literal();
    test_ir_exec_typed_lookback_brackets_are_stack_neutral();
    printf("  \xE2\x9C\x93 ir_exec_typed\n");
    return 0;
}
