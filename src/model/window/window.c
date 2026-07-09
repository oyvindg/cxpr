#include "model/window/window.h"

#include "eval/internal.h"
#include "lookback.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool cxpr_model_window_is_function(const char* name) {
    return name &&
           (strcmp(name, "window_sum") == 0 ||
            strcmp(name, "window_mean") == 0 ||
            strcmp(name, "window_highest") == 0 ||
            strcmp(name, "window_lowest") == 0 ||
            strcmp(name, "window_stddev") == 0 ||
            strcmp(name, "window_roc") == 0);
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

static bool cxpr_model_window_period_depth(const cxpr_model* model,
                                           const cxpr_ast* period_ast,
                                           size_t* out_depth,
                                           cxpr_error* err) {
    double raw = 0.0;
    long period;
    if (!period_ast || !out_depth) return false;
    if (cxpr_ast_type(period_ast) == CXPR_NODE_VARIABLE) {
        if (!cxpr_model_window_constant_default(
                model, cxpr_ast_variable_name(period_ast), &raw)) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "window period parameter requires a numeric default",
                                 0, 0);
            return false;
        }
    } else if (!cxpr_eval_constant_double(period_ast, &raw)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "window period must be a constant or model parameter default",
                             0, 0);
        return false;
    }
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
            return false;
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
            size_t period_depth = 0u;
            size_t extra_depth = 0u;
            if (cxpr_ast_function_argc(ast) != 2u) {
                cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                                     "window function expects two arguments", 0, 0);
                return false;
            }
            if (!cxpr_model_window_period_depth(
                    model, cxpr_ast_function_arg(ast, 1u), &period_depth, err)) {
                return false;
            }
            extra_depth = cxpr_model_names_match(cxpr_ast_function_name(ast), "window_roc")
                ? period_depth
                : (period_depth > 0u ? period_depth - 1u : 0u);
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
    size_t period_depth = 0u;
    if (!call || cxpr_ast_type(call) != CXPR_NODE_FUNCTION_CALL) return true;
    name = cxpr_ast_function_name(call);
    if (!cxpr_model_window_is_function(name)) return true;
    if (cxpr_ast_function_argc(call) != 2u) {
        cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                             "window function expects two arguments", 0, 0);
        return false;
    }
    if (!cxpr_model_window_period_depth(
            model, cxpr_ast_function_arg(call, 1u), &period_depth, err)) {
        return false;
    }
    if (period_depth == 0u) return true;
    if (cxpr_model_names_match(name, "window_roc")) {
        return cxpr_model_window_collect_target(
            model, cxpr_ast_function_arg(call, 0u), period_depth, specs, count, err);
    }
    return cxpr_model_window_collect_target(
        model, cxpr_ast_function_arg(call, 0u),
        period_depth > 0u ? period_depth - 1u : 0u,
        specs, count, err);
}
