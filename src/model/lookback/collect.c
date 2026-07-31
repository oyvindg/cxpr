/**
 * @file model/lookback/collect.c
 * @brief Model lookback validation and history requirement collection.
 */

#include "lookback.h"
#include "model/internal.h"
#include "model/window/window.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_history_spec_add(cxpr_model_history_spec** specs,
                                        size_t* count,
                                        const char* name,
                                        const cxpr_expr_ast* target,
                                        size_t depth) {
    cxpr_model_history_spec* grown;
    if (!specs || !count || !name || depth == 0u) return true;
    for (size_t i = 0; i < *count; ++i) {
        if (cxpr_model_names_match((*specs)[i].name, name)) {
            if ((*specs)[i].depth < depth) (*specs)[i].depth = depth;
            if (!(*specs)[i].target && target) {
                (*specs)[i].target = cxpr_expr_ast_clone(target);
                if (!(*specs)[i].target) return false;
            }
            return true;
        }
    }
    grown = (cxpr_model_history_spec*)realloc(
        *specs, (*count + 1u) * sizeof(cxpr_model_history_spec));
    if (!grown) return false;
    *specs = grown;
    (*specs)[*count].name = cxpr_strdup(name);
    (*specs)[*count].target = target ? cxpr_expr_ast_clone(target) : NULL;
    (*specs)[*count].depth = depth;
    if (!(*specs)[*count].name || (target && !(*specs)[*count].target)) return false;
    (*count)++;
    return true;
}

bool cxpr_model_lookback_target_key(const cxpr_expr_ast* target,
                                    char** out_key,
                                    cxpr_error* err) {
    if (out_key) *out_key = NULL;
    if (!target || !out_key) return false;
    switch (cxpr_expr_ast_kind_of(target)) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
        *out_key = cxpr_expr_ast_to_string(target);
        if (!*out_key) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        return true;
    default:
        return false;
    }
}

static bool cxpr_model_lookback_bound_impl(const cxpr_model* model,
                                           const cxpr_expr_ast* index,
                                           size_t* out_bound,
                                           cxpr_error* err,
                                           unsigned depth) {
    double value;
    if (!model || !index || !out_bound || depth > 32u) return false;
    switch (cxpr_expr_ast_kind_of(index)) {
    case CXPR_NODE_NUMBER:
        value = cxpr_expr_ast_number_value(index);
        if (!isfinite(value) || value < 0.0 || value > (double)SIZE_MAX) return false;
        *out_bound = (size_t)ceil(value);
        return true;
    case CXPR_NODE_VARIABLE: {
        const char* name = cxpr_expr_ast_param_name(index);
        for (size_t i = 0u; i < model->constant_count; ++i) {
            double minimum = 0.0;
            double maximum;
            bool has_minimum = false;
            bool has_maximum = false;
            if (!cxpr_model_names_match(model->constants[i].name, name)) continue;
            for (size_t m = 0u; m < model->metadata_count; ++m) {
                if (model->metadatas[m].target_kind != CXPR_MODEL_METADATA_TARGET_PARAM ||
                    !cxpr_model_names_match(model->metadatas[m].target_name, name)) {
                    continue;
                }
                has_minimum =
                    cxpr_model_metadata_field_number(model, m, "min", &minimum);
                has_maximum =
                    cxpr_model_metadata_field_number(model, m, "max", &maximum);
                break;
            }
            if (model->constants[i].is_call_param) {
                double default_value;
                if (!has_minimum || !has_maximum) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback parameter requires min and max metadata", 0, 0);
                    return false;
                }
                if (!isfinite(minimum) || minimum < 1.0 || floor(minimum) != minimum) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback parameter min must be an integer >= 1", 0, 0);
                    return false;
                }
                if (!isfinite(maximum) || maximum < minimum ||
                    floor(maximum) != maximum || maximum > (double)SIZE_MAX) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback parameter max must be a finite integer >= min", 0, 0);
                    return false;
                }
                if (!model->constants[i].expr ||
                    cxpr_expr_ast_kind_of(model->constants[i].expr) != CXPR_NODE_NUMBER) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback parameter default must be an integer literal", 0, 0);
                    return false;
                }
                default_value = cxpr_expr_ast_number_value(model->constants[i].expr);
                if (!isfinite(default_value) || floor(default_value) != default_value ||
                    default_value < minimum || default_value > maximum) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback parameter default must be within min and max", 0, 0);
                    return false;
                }
                *out_bound = (size_t)maximum;
                return true;
            }
            return cxpr_model_lookback_bound_impl(
                model, model->constants[i].expr, out_bound, err, depth + 1u);
        }
        return false;
    }
    case CXPR_NODE_IDENTIFIER: {
        const char* name = cxpr_expr_ast_identifier_name(index);
        for (size_t i = 0u; i < model->binding_count; ++i) {
            if (cxpr_model_names_match(model->bindings[i].name, name)) {
                return cxpr_model_lookback_bound_impl(
                    model, model->bindings[i].expr, out_bound, err, depth + 1u);
            }
        }
        return false;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_expr_ast_call_name(index);
        size_t argc = cxpr_expr_ast_call_arg_count(index);
        size_t bound = 0u;
        if ((cxpr_model_names_match(name, "round") ||
             cxpr_model_names_match(name, "floor") ||
             cxpr_model_names_match(name, "ceil")) &&
            argc == 1u) {
            return cxpr_model_lookback_bound_impl(
                model, cxpr_expr_ast_call_arg(index, 0u), out_bound, err, depth + 1u);
        }
        if ((!cxpr_model_names_match(name, "max") &&
             !cxpr_model_names_match(name, "min")) ||
            argc == 0u) {
            return false;
        }
        for (size_t i = 0u; i < argc; ++i) {
            size_t arg_bound;
            if (!cxpr_model_lookback_bound_impl(
                    model, cxpr_expr_ast_call_arg(index, i), &arg_bound, err, depth + 1u)) {
                return false;
            }
            if (i == 0u || cxpr_model_names_match(name, "max")) {
                if (i == 0u || arg_bound > bound) bound = arg_bound;
            } else if (arg_bound < bound) {
                bound = arg_bound;
            }
        }
        *out_bound = bound;
        return true;
    }
    case CXPR_NODE_BINARY_OP: {
        size_t left;
        size_t right;
        int op = cxpr_expr_ast_operator(index);
        if (!cxpr_model_lookback_bound_impl(
                model, cxpr_expr_ast_binary_left(index), &left, err, depth + 1u) ||
            !cxpr_model_lookback_bound_impl(
                model, cxpr_expr_ast_binary_right(index), &right, err, depth + 1u)) {
            return false;
        }
        if (op == CXPR_TOK_PLUS) {
            if (left > SIZE_MAX - right) return false;
            *out_bound = left + right;
            return true;
        }
        if (op == CXPR_TOK_MINUS) {
            *out_bound = left;
            return true;
        }
        if (op == CXPR_TOK_STAR) {
            if (right != 0u && left > SIZE_MAX / right) return false;
            *out_bound = left * right;
            return true;
        }
        if (op == CXPR_TOK_SLASH) {
            double divisor;
            if (cxpr_expr_ast_kind_of(cxpr_expr_ast_binary_right(index)) != CXPR_NODE_NUMBER) {
                return false;
            }
            divisor = cxpr_expr_ast_number_value(cxpr_expr_ast_binary_right(index));
            if (!isfinite(divisor) || divisor <= 0.0) return false;
            *out_bound = (size_t)ceil((double)left / divisor);
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

bool cxpr_model_lookback_bound(const cxpr_model* model,
                               const cxpr_expr_ast* index,
                               size_t* out_bound,
                               cxpr_error* err) {
    return cxpr_model_lookback_bound_impl(model, index, out_bound, err, 0u);
}

static bool cxpr_model_collect_lookbacks_in_ast(const cxpr_model* model,
                                                const cxpr_expr_ast* ast,
                                                cxpr_model_history_spec** specs,
                                                size_t* count,
                                                cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < cxpr_expr_ast_record_field_count(ast); ++i) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    model, cxpr_expr_ast_record_field_value(ast, i), specs, count, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK: {
        const cxpr_expr_ast* target = cxpr_expr_ast_lookback_target(ast);
        const cxpr_expr_ast* index = cxpr_expr_ast_lookback_index(ast);
        unsigned offset = 0u;
        size_t dynamic_bound = 0u;
        if (!cxpr_lookback_literal_offset(index, &offset, NULL, NULL)) {
            if (!cxpr_model_lookback_bound(model, index, &dynamic_bound, err) ||
                dynamic_bound > (size_t)(~0u)) {
                if (!err || err->code == CXPR_OK) {
                    cxpr_model_set_error(
                        err, CXPR_ERR_SYNTAX,
                        "dynamic lookback requires bounded integer metadata", 0, 0);
                }
                return false;
            }
            offset = (unsigned)dynamic_bound;
        }
        {
            char* key = NULL;
            bool supported = cxpr_model_lookback_target_key(target, &key, err);
            if (supported &&
                !cxpr_model_history_spec_add(specs, count, key, target, (size_t)offset)) {
                free(key);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            free(key);
        }
        return cxpr_model_collect_lookbacks_in_ast(model, target, specs, count, err);
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_collect_lookbacks_in_ast(model, cxpr_expr_ast_binary_left(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(model, cxpr_expr_ast_binary_right(ast), specs, count, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_collect_lookbacks_in_ast(model, cxpr_expr_ast_unary_operand(ast), specs, count, err);
    case CXPR_NODE_FUNCTION_CALL:
        if (cxpr_model_window_is_function(cxpr_expr_ast_call_name(ast)) &&
            !cxpr_model_window_collect_call(model, ast, specs, count, err)) {
            return false;
        }
        if ((cxpr_model_names_match(cxpr_expr_ast_call_name(ast), "cross_above") ||
             cxpr_model_names_match(cxpr_expr_ast_call_name(ast), "cross_below")) &&
            cxpr_expr_ast_call_arg_count(ast) == 2u) {
            for (size_t i = 0u; i < 2u; ++i) {
                const cxpr_expr_ast* arg = cxpr_expr_ast_call_arg(ast, i);
                char* key = NULL;
                bool supported = cxpr_model_lookback_target_key(arg, &key, err);
                if (supported &&
                    !cxpr_model_history_spec_add(specs, count, key, arg, 1u)) {
                    free(key);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                free(key);
            }
        }
        for (size_t i = 0; i < cxpr_expr_ast_call_arg_count(ast); ++i) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    model, cxpr_expr_ast_call_arg(ast, i), specs, count, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    model, cxpr_expr_ast_producer_arg(ast, i), specs, count, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_TERNARY:
        return cxpr_model_collect_lookbacks_in_ast(
                   model, cxpr_expr_ast_ternary_condition(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(
                   model, cxpr_expr_ast_ternary_true(ast), specs, count, err) &&
               cxpr_model_collect_lookbacks_in_ast(
                   model, cxpr_expr_ast_ternary_false(ast), specs, count, err);
    default:
        return true;
    }
}

bool cxpr_model_collect_lookbacks(const cxpr_model* model,
                                  const cxpr_registry* registry,
                                  cxpr_model_history_spec** specs,
                                  size_t* count,
                                  cxpr_error* err) {
    if (!model || !specs || !count) return true;
    for (size_t i = 0; i < model->constant_count; ++i) {
        cxpr_expr_ast* expanded =
            cxpr_model_inline_defined_calls(model->constants[i].expr, registry, err);
        bool ok = expanded &&
            cxpr_model_collect_lookbacks_in_ast(model, expanded, specs, count, err);
        cxpr_expr_ast_free(expanded);
        if (!ok) {
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        cxpr_expr_ast* expanded =
            cxpr_model_inline_defined_calls(model->bindings[i].expr, registry, err);
        bool ok = expanded &&
            cxpr_model_collect_lookbacks_in_ast(model, expanded, specs, count, err);
        cxpr_expr_ast_free(expanded);
        if (!ok) {
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_collect_lookbacks_in_ast(
                    model, model->record_functions[i].fields[f].expr, specs, count, err)) {
                return false;
            }
        }
    }
    return true;
}
