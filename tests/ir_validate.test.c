#include <assert.h>
#include <stdio.h>

bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program);

static void test_ir_validate_scalar_programs(void) {
    cxpr_ir_instr ok_code[] = {
        {.op = CXPR_OP_PUSH_CONST, .value = 2.0},
        {.op = CXPR_OP_PUSH_CONST, .value = 3.0},
        {.op = CXPR_OP_ADD},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_instr bad_code[] = {
        {.op = CXPR_OP_PUSH_CONST, .value = 2.0},
        {.op = CXPR_OP_ADD},
        {.op = CXPR_OP_RETURN}
    };
    cxpr_ir_program ok = {.code = ok_code, .count = 4};
    cxpr_ir_program bad = {.code = bad_code, .count = 3};

    assert(cxpr_ir_validate_scalar_fast_program(&ok));
    assert(!cxpr_ir_validate_scalar_fast_program(&bad));
}

int main(void) {
    test_ir_validate_scalar_programs();
    printf("  \xE2\x9C\x93 ir_validate\n");
    return 0;
}
