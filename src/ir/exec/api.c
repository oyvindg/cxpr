/**
 * @file api.c
 * @brief Public IR execution API wrappers.
 */

#include "internal.h"
#include <math.h>

bool cxpr_ir_prepare_defined_program(cxpr_func_entry* entry, const cxpr_registry* reg,
                                     cxpr_error* err) {
    if (!entry || !entry->defined_body || !cxpr_ir_defined_is_scalar_only(entry)) {
        return false;
    }
    if (entry->defined_program || entry->defined_program_failed) {
        return entry->defined_program != NULL;
    }
    if (err) *err = (cxpr_error){0};

    entry->defined_program = (cxpr_program*)calloc(1, sizeof(cxpr_program));
    if (!entry->defined_program) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    if (!cxpr_ir_compile_with_locals(entry->defined_body, reg,
                                     (const char* const*)entry->defined_param_names,
                                     entry->defined_param_count, &entry->defined_program->ir,
                                     err)) {
        entry->defined_program_failed = true;
        cxpr_program_free(entry->defined_program);
        entry->defined_program = NULL;
        return false;
    }

    entry->defined_program->ast = entry->defined_body;
    return true;
}

double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, locals, local_count, err);
    }
    cxpr_value value = cxpr_ir_exec_typed(program, ctx, reg, locals, local_count, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return NAN;
    }
    return value.d;
}

double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, NULL, 0, err);
    }
    cxpr_value value = cxpr_ir_exec_typed(program, ctx, reg, NULL, 0, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return NAN;
    }
    return value.d;
}

cxpr_value cxpr_eval_program_value(const cxpr_program* prog, const cxpr_context* ctx,
                                   const cxpr_registry* reg, cxpr_error* err) {
    if (!prog) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL compiled program";
        }
        return cxpr_fv_double(NAN);
    }
    return cxpr_ir_exec_typed(&prog->ir, ctx, reg, NULL, 0, err);
}

bool cxpr_eval_program(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    *out_value = value;
    return true;
}

bool cxpr_eval_program_number(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, double* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = fast_value;
        return true;
    }
    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = fast_value;
        return true;
    }
    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return false;
    }
    *out_value = value.d;
    return true;
}

bool cxpr_eval_program_bool(const cxpr_program* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, bool* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = (fast_value != 0.0);
        return true;
    }
    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to bool";
        }
        return false;
    }
    *out_value = value.b;
    return true;
}
