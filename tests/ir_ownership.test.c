/**
 * @file ir_ownership.test.c
 * @brief Focused IR ownership/reset test for sanitizer runs.
 */

#include "internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define EPSILON 1e-10

static void test_ir_compile_reset_struct_defined_function_repeatedly(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    const char* fields[] = {"x", "y"};
    double left_vals[] = {3.0, 4.0};
    double right_vals[] = {0.0, 0.0};
    cxpr_ast* ast;

    assert(p);
    assert(ctx);
    assert(reg);
    cxpr_register_builtins(reg);
    assert(cxpr_registry_define_fn(reg,
                                   "dist2(p, q) => sqrt((p.x - q.x)*(p.x - q.x) + "
                                   "(p.y - q.y)*(p.y - q.y))")
               .code == CXPR_OK);
    cxpr_context_set_fields(ctx, "lhs", fields, left_vals, 2);
    cxpr_context_set_fields(ctx, "rhs", fields, right_vals, 2);

    ast = cxpr_parse(p, "dist2(lhs, rhs) + dist2(lhs, rhs)", &err);
    assert(ast);

    for (size_t i = 0; i < 256; ++i) {
        cxpr_ir_program program = {0};
        double result;

        assert(cxpr_ir_compile(ast, reg, &program, &err) == true);
        assert(err.code == CXPR_OK);
        result = cxpr_ir_exec(&program, ctx, reg, &err);
        assert(err.code == CXPR_OK);
        assert(fabs(result - 10.0) < EPSILON);
        cxpr_ir_program_reset(&program);
    }

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ir_compile_reset_struct_defined_function_repeatedly\n");
}

int main(void) {
    printf("Running IR ownership tests...\n");
    test_ir_compile_reset_struct_defined_function_repeatedly();
    printf("All IR ownership tests passed!\n");
    return 0;
}
