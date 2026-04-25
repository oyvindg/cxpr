#include <assert.h>
#include <stdio.h>

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

int main(void) {
    test_ir_exec_typed_bool_path();
    printf("  \xE2\x9C\x93 ir_exec_typed\n");
    return 0;
}
