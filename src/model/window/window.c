#include "model/window/window.h"

#include "eval/internal.h"
#include "lookback.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool cxpr_model_window_is_function(const char* name) {
    return cxpr_window_ir_find(name) != NULL;
}

static bool cxpr_model_window_constant_default(const cxpr_model* model,
                                               const char* name,
                                               double* out) {
    if (!model || !name || !out) return false;
    for (size_t i = 0u; i < model->constant_count; ++i) {
        if (cxpr_model_names_match(model->constants[i].name, name)) {
            return cxpr_eval_constant_double(model->constants[i].expr, out);
        }
    }
    return false;
}

static bool cxpr_model_window_constant_expr(const cxpr_model* model,
                                            const cxpr_ast* ast,
                                            double* out) {
    double left = 0.0;
    double right = 0.0;
    int op;
    if (!ast || !out) return false;
    if (cxpr_eval_constant_double(ast, out)) return true;
    if (cxpr_ast_type(ast) == CXPR_NODE_VARIABLE) {
        return cxpr_model_window_constant_default(
            model, cxpr_ast_variable_name(ast), out);
    }
    if (cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_ast_function_name(ast);
        size_t argc = cxpr_ast_function_argc(ast);
        if ((!cxpr_model_names_match(name, "min") &&
             !cxpr_model_names_match(name, "max")) || argc == 0u ||
            !cxpr_model_window_constant_expr(
                model, cxpr_ast_function_arg(ast, 0u), out)) {
            return false;
        }
        for (size_t i = 1u; i < argc; ++i) {
            double value = 0.0;
            if (!cxpr_model_window_constant_expr(
                    model, cxpr_ast_function_arg(ast, i), &value)) {
                return false;
            }
            *out = cxpr_model_names_match(name, "min")
                       ? fmin(*out, value)
                       : fmax(*out, value);
        }
        return true;
    }
    if (cxpr_ast_type(ast) != CXPR_NODE_BINARY_OP) return false;
    if (!cxpr_model_window_constant_expr(model, cxpr_ast_left(ast), &left) ||
        !cxpr_model_window_constant_expr(model, cxpr_ast_right(ast), &right)) {
        return false;
    }
    op = cxpr_ast_operator(ast);
    if (op == CXPR_TOK_PLUS) *out = left + right;
    else if (op == CXPR_TOK_MINUS) *out = left - right;
    else if (op == CXPR_TOK_STAR) *out = left * right;
    else if (op == CXPR_TOK_SLASH && fabs(right) > 1e-12) *out = left / right;
    else return false;
    return true;
}

static bool cxpr_model_window_period_depth(const cxpr_model* model,
                                           const cxpr_ast* period_ast,
                                           size_t* out_depth,
                                           cxpr_error* err) {
    double raw = 0.0;
    long period;
    (void)err;
    if (!period_ast || !out_depth) return false;
    if (cxpr_ast_type(period_ast) == CXPR_NODE_VARIABLE) {
        const char* param_name = cxpr_ast_variable_name(period_ast);
        for (size_t i = 0u; i < cxpr_model_metadata_count(model); ++i) {
            const char* target;
            if (cxpr_model_metadata_target_kind_at(model, i) !=
                CXPR_MODEL_METADATA_TARGET_PARAM) continue;
            target = cxpr_model_metadata_target_name(model, i);
            if (target && cxpr_model_names_match(target, param_name) &&
                cxpr_model_metadata_field_number(model, i, "max", &raw)) {
                goto resolved;
            }
        }
    }
    if (!cxpr_model_window_constant_expr(model, period_ast, &raw)) raw = 512.0;
resolved:
    if (!isfinite(raw) || raw < 1.0) raw = 1.0;
    period = lround(raw);
    if (period < 1) period = 1;
    *out_depth = (size_t)period;
    return true;
}

static bool cxpr_model_window_history_spec_add(cxpr_model_history_spec** specs,
                                               size_t* count,
                                               const char* name,
                                               const cxpr_ast* target,
                                               size_t depth,
                                               cxpr_error* err) {
    cxpr_model_history_spec* grown;
    if (!specs || !count || !name || depth == 0u) return true;
    for (size_t i = 0u; i < *count; ++i) {
        if (cxpr_model_names_match((*specs)[i].name, name)) {
            if (depth > (*specs)[i].depth) (*specs)[i].depth = depth;
            if (!(*specs)[i].target && target) {
                (*specs)[i].target = cxpr_ast_clone(target);
                if (!(*specs)[i].target) {
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
            }
            return true;
        }
    }
    grown = (cxpr_model_history_spec*)realloc(
        *specs, (*count + 1u) * sizeof(cxpr_model_history_spec));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    *specs = grown;
    (*specs)[*count].name = cxpr_strdup(name);
    (*specs)[*count].target = target ? cxpr_ast_clone(target) : NULL;
    (*specs)[*count].depth = depth;
    if (!(*specs)[*count].name || (target && !(*specs)[*count].target)) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    (*count)++;
    return true;
}

static bool cxpr_model_window_collect_leaf(const cxpr_ast* ast,
                                           size_t depth,
                                           cxpr_model_history_spec** specs,
                                           size_t* count,
                                           cxpr_error* err) {
    char* key = NULL;
    if (depth == 0u) return true;
    if (!cxpr_model_lookback_target_key(ast, &key, NULL)) return true;
    {
        bool ok = cxpr_model_window_history_spec_add(specs, count, key, ast, depth, err);
        free(key);
        return ok;
    }
}

static bool cxpr_model_window_collect_target(const cxpr_model* model,
                                             const cxpr_ast* ast,
                                             size_t base_depth,
                                             cxpr_model_history_spec** specs,
                                             size_t* count,
                                             cxpr_error* err) {
    (void)model;
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
        if (!cxpr_model_window_collect_leaf(ast, base_depth, specs, count, err)) return false;
        if (cxpr_ast_type(ast) == CXPR_NODE_PRODUCER_ACCESS) {
            for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
                if (!cxpr_model_window_collect_target(
                        model, cxpr_ast_producer_arg(ast, i), base_depth, specs, count, err)) {
                    return false;
                }
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK: {
        unsigned offset = 0u;
        if (!cxpr_lookback_literal_offset(cxpr_ast_lookback_index(ast), &offset, err,
                                          "window lookback requires constant integer index")) {
            size_t dynamic_bound = 0u;
            if (!cxpr_model_lookback_bound(
                    model, cxpr_ast_lookback_index(ast), &dynamic_bound, err) ||
                dynamic_bound > (size_t)(~0u)) {
                if (err && err->code == CXPR_OK) {
                    err->code = CXPR_ERR_SYNTAX;
                    err->message = "window dynamic lookback requires bounded integer metadata";
                }
                return false;
            }
            offset = (unsigned)dynamic_bound;
            if (err) *err = (cxpr_error){0};
        }
        return cxpr_model_window_collect_target(
            model, cxpr_ast_lookback_target(ast), base_depth + (size_t)offset,
            specs, count, err);
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_window_collect_target(model, cxpr_ast_left(ast), base_depth, specs, count, err) &&
               cxpr_model_window_collect_target(model, cxpr_ast_right(ast), base_depth, specs, count, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_window_collect_target(model, cxpr_ast_operand(ast), base_depth, specs, count, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_window_collect_target(model, cxpr_ast_ternary_condition(ast), base_depth, specs, count, err) &&
               cxpr_model_window_collect_target(model, cxpr_ast_ternary_true_branch(ast), base_depth, specs, count, err) &&
               cxpr_model_window_collect_target(model, cxpr_ast_ternary_false_branch(ast), base_depth, specs, count, err);
    case CXPR_NODE_FUNCTION_CALL:
        if (cxpr_model_window_is_function(cxpr_ast_function_name(ast))) {
            const char* name = cxpr_ast_function_name(ast);
            size_t period_depth = 0u;
            size_t extra_depth = 0u;
            const cxpr_window_ir* window = cxpr_window_ir_find(name);
            const size_t expected_argc = window ? window->arity : 0u;
            if (cxpr_ast_function_argc(ast) != expected_argc) {
                cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                                     "window function has wrong arity", 0, 0);
                return false;
            }
            if (!cxpr_model_window_period_depth(
                    model, cxpr_ast_function_arg(ast, 1u), &period_depth, err)) {
                return false;
            }
            extra_depth = period_depth + window->history_tail;
            if (extra_depth > 0u) extra_depth--;
            return cxpr_model_window_collect_target(
                model, cxpr_ast_function_arg(ast, 0u), base_depth + extra_depth,
                specs, count, err);
        }
        for (size_t i = 0u; i < cxpr_ast_function_argc(ast); ++i) {
            if (!cxpr_model_window_collect_target(
                    model, cxpr_ast_function_arg(ast, i), base_depth, specs, count, err)) {
                return false;
            }
        }
        return true;
    default:
        return true;
    }
}

bool cxpr_model_window_collect_call(const cxpr_model* model,
                                    const cxpr_ast* call,
                                    cxpr_model_history_spec** specs,
                                    size_t* count,
                                    cxpr_error* err) {
    const char* name;
    const cxpr_window_ir* window;
    size_t period_depth = 0u;
    if (!call || cxpr_ast_type(call) != CXPR_NODE_FUNCTION_CALL) return true;
    name = cxpr_ast_function_name(call);
    if (!cxpr_model_window_is_function(name)) return true;
    window = cxpr_window_ir_find(name);
    if (!window || cxpr_ast_function_argc(call) != window->arity) {
        cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                             "window function has wrong arity", 0, 0);
        return false;
    }
    if (!cxpr_model_window_period_depth(
            model, cxpr_ast_function_arg(call, 1u), &period_depth, err)) {
        return false;
    }
    if (period_depth == 0u) return true;
    return cxpr_model_window_collect_target(
        model, cxpr_ast_function_arg(call, 0u),
        period_depth + window->history_tail - 1u,
        specs, count, err);
}
