#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

bool cxpr_ir_compile_with_locals(const cxpr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err);
void cxpr_ir_program_reset(cxpr_ir_program* program);

static void test_ir_compile_with_locals_and_fast_kind(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_ir_program program = {0};
    const char* locals[] = {"x"};

    assert(p && reg);
    ast = cxpr_parse(p, "x * 2", &err);
    assert(ast);
    assert(cxpr_ir_compile_with_locals(ast, reg, locals, 1, &program, &err));
    assert(program.count > 0);
    assert(program.fast_result_kind == 1);
    assert(program.code[0].op == CXPR_OP_LOAD_LOCAL || program.code[0].op == CXPR_OP_PUSH_CONST);

    cxpr_ir_program_reset(&program);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_parser_free(p);
}

int main(void) {
    test_ir_compile_with_locals_and_fast_kind();
    printf("  \xE2\x9C\x93 ir_compile\n");
    return 0;
}
