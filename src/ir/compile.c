/**
 * @file compile.c
 * @brief IR compilation entry points for cxpr.
 */

#include "compile/internal.h"

bool cxpr_ir_compile_with_locals(const cxpr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!program) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL IR program";
        }
        return false;
    }

    cxpr_ir_program_reset(program);
    program->ast = ast;
    program->fast_result_kind = cxpr_ir_infer_fast_result_kind(ast, reg, 0);

    if (!cxpr_ir_compile_node(ast, program, reg, local_names, local_count,
                              NULL, 0, err)) {
        cxpr_ir_program_reset(program);
        return false;
    }

    if (!cxpr_ir_emit(program,
                      (cxpr_ir_instr){
                          .op = CXPR_OP_RETURN,
                          .value = 0.0,
                          .name = NULL,
                      },
                      err)) {
        cxpr_ir_program_reset(program);
        return false;
    }

    program->lookup_cache =
        (cxpr_ir_lookup_cache*)calloc(program->count, sizeof(cxpr_ir_lookup_cache));
    if (program->count > 0 && !program->lookup_cache) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        cxpr_ir_program_reset(program);
        return false;
    }

    if (program->fast_result_kind == CXPR_IR_RESULT_BOOL) {
        if (!cxpr_ir_validate_bool_fast_program(program)) {
            program->fast_result_kind = CXPR_IR_RESULT_UNKNOWN;
        }
    } else if (program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        if (!cxpr_ir_validate_scalar_fast_program(program)) {
            program->fast_result_kind = CXPR_IR_RESULT_UNKNOWN;
        }
    }

    return true;
}

bool cxpr_ir_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_ir_program* program,
                     cxpr_error* err) {
    return cxpr_ir_compile_with_locals(ast, reg, NULL, 0, program, err);
}
