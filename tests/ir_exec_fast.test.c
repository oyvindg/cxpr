#include <assert.h>
#include <math.h>
#include <stdio.h>

double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);

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

int main(void) {
    test_ir_exec_fast_scalar_path();
    printf("  \xE2\x9C\x93 ir_exec_fast\n");
    return 0;
}
