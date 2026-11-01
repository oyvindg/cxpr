/**
 * @file compile.c
 * @brief IR compilation entry points for cxpr.
 */

#include "compile/internal.h"

bool cxpr_ir_compile_with_locals(const cxpr_expr_ast* ast, const cxpr_registry* reg,
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

    if (!cxpr_ir_program_prepare_inputs(program, err)) {
        cxpr_ir_program_reset(program);
        return false;
    }

    program->int64_fast_compatible = cxpr_ir_validate_int64_fast_program(program);
    if (!cxpr_ir_program_lower_value_kinds(program, err)) {
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
    } else if (program->fast_result_kind == CXPR_IR_RESULT_INT64) {
        /* Unsupported dynamic int64 programs retain the typed fallback. */
    }

    if (program->fast_result_kind == CXPR_IR_RESULT_UNKNOWN) {
        bool has_resolved_lookback = false;
        for (size_t i = 0; i < program->count; ++i) {
            has_resolved_lookback |= program->code[i].op == CXPR_OP_LOOKBACK_RESOLVE;
        }
        program->number_fast_compatible =
            has_resolved_lookback && cxpr_ir_validate_scalar_fast_program(program);
    }

    return true;
}

bool cxpr_ir_compile(const cxpr_expr_ast* ast, const cxpr_registry* reg, cxpr_ir_program* program,
                     cxpr_error* err) {
    return cxpr_ir_compile_with_locals(ast, reg, NULL, 0, program, err);
}
