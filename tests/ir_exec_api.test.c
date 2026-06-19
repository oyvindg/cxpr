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

int main(void) {
    test_ir_exec_api_wrappers();
    test_ir_runtime_error_helper();
    printf("  \xE2\x9C\x93 ir_exec_api\n");
    return 0;
}
