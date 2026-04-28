/**
 * @file api.c
 * @brief Public evaluator API wrappers.
 */

#include "internal.h"
#include <math.h>

cxpr_value cxpr_eval_ast_value(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err) {
    cxpr_value value;
    if (err) *err = (cxpr_error){0};
    cxpr_eval_memo_enter((cxpr_context*)ctx);
    value = cxpr_eval_node(ast, ctx, reg, err);
    cxpr_eval_memo_leave((cxpr_context*)ctx);
    return value;
}

bool cxpr_eval_ast(const cxpr_ast* ast, const cxpr_context* ctx,
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

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    *out_value = value;
    return true;
}

bool cxpr_eval_ast_number(const cxpr_ast* ast, const cxpr_context* ctx,
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

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
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

bool cxpr_eval_ast_bool(const cxpr_ast* ast, const cxpr_context* ctx,
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

    value = cxpr_eval_ast_value(ast, ctx, reg, err);
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

bool cxpr_eval_ast_at_lookback(const cxpr_ast* ast,
                               const cxpr_ast* index_ast,
                               const cxpr_context* ctx,
                               const cxpr_registry* reg,
                               cxpr_value* out_value,
                               cxpr_error* err) {
    cxpr_ast* target_copy = NULL;
    cxpr_ast* index_copy = NULL;
    cxpr_ast* lookback_ast = NULL;
    bool ok = false;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!ast || !index_ast) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback evaluation requires target and index";
        }
        return false;
    }

    target_copy = cxpr_eval_clone_ast(ast);
    index_copy = cxpr_eval_clone_ast(index_ast);
    if (!target_copy || !index_copy) {
        cxpr_ast_free(target_copy);
        cxpr_ast_free(index_copy);
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    lookback_ast = cxpr_ast_new_lookback(target_copy, index_copy);
    if (!lookback_ast) {
        cxpr_ast_free(target_copy);
        cxpr_ast_free(index_copy);
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    ok = cxpr_eval_ast(lookback_ast, ctx, reg, out_value, err);
    cxpr_ast_free(lookback_ast);
    return ok;
}

bool cxpr_eval_ast_at_offset(const cxpr_ast* ast,
                             double lookback,
                             const cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_value* out_value,
                             cxpr_error* err) {
    cxpr_ast* index_ast = NULL;
    bool ok = false;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!isfinite(lookback) || lookback < 0.0) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback offset must be a finite non-negative number";
        }
        return false;
    }

    index_ast = cxpr_ast_new_number(lookback);
    if (!index_ast) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    ok = cxpr_eval_ast_at_lookback(ast, index_ast, ctx, reg, out_value, err);
    cxpr_ast_free(index_ast);
    return ok;
}

bool cxpr_eval_ast_number_at_offset(const cxpr_ast* ast,
                                    double lookback,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    double* out_value,
                                    cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!cxpr_eval_ast_at_offset(ast, lookback, ctx, reg, &value, err)) return false;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expected number";
        }
        return false;
    }
    *out_value = value.d;
    return true;
}

bool cxpr_eval_ast_bool_at_offset(const cxpr_ast* ast,
                                  double lookback,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  bool* out_value,
                                  cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!cxpr_eval_ast_at_offset(ast, lookback, ctx, reg, &value, err)) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expected bool";
        }
        return false;
    }
    *out_value = value.b;
    return true;
}
