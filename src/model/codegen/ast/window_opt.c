#include "model/codegen/ast/internal.h"
#include "model/window/plan.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <cxpr/resample.h>
#include "eval/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* cxpr_model_c_window_op(const char* name) {
    static const char* const codes[] = {"0", "1", "2", "3", "4", "5"};
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    return window && window->reduction >= CXPR_WINDOW_REDUCE_SUM &&
                   window->reduction <= CXPR_WINDOW_REDUCE_WEIGHTED_MEAN
               ? codes[window->reduction]
               : NULL;
}

bool cxpr_model_c_constant_param_expr(const cxpr_model_compiled* program,
                                             const cxpr_expr_ast* ast,
                                             double* out) {
    double left = 0.0;
    double right = 0.0;
    int op;
    if (!ast || !out) return false;
    if (cxpr_eval_constant_double(ast, out)) return true;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_expr_ast_param_name(ast);
        size_t index = cxpr_model_compiled_param_index(program, name);
        return index != (size_t)-1 &&
               program->constants[index].ast &&
               cxpr_eval_constant_double(program->constants[index].ast, out);
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_expr_ast_call_name(ast);
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if ((!cxpr_model_names_match(name, "min") &&
             !cxpr_model_names_match(name, "max")) || argc == 0u ||
            !cxpr_model_c_constant_param_expr(
                program, cxpr_expr_ast_call_arg(ast, 0u), out)) {
            return false;
        }
        for (size_t i = 1u; i < argc; ++i) {
            double value = 0.0;
            if (!cxpr_model_c_constant_param_expr(
                    program, cxpr_expr_ast_call_arg(ast, i), &value)) {
                return false;
            }
            *out = cxpr_model_names_match(name, "min")
                       ? fmin(*out, value)
                       : fmax(*out, value);
        }
        return true;
    }
    if (cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP) return false;
    if (!cxpr_model_c_constant_param_expr(program, cxpr_expr_ast_binary_left(ast), &left) ||
        !cxpr_model_c_constant_param_expr(program, cxpr_expr_ast_binary_right(ast), &right)) {
        return false;
    }
    op = cxpr_expr_ast_operator(ast);
    if (op == CXPR_TOK_PLUS) *out = left + right;
    else if (op == CXPR_TOK_MINUS) *out = left - right;
    else if (op == CXPR_TOK_STAR) *out = left * right;
    else if (op == CXPR_TOK_SLASH && fabs(right) > 1e-12) *out = left / right;
    else return false;
    return true;
}

bool cxpr_model_c_window_period_capacity(const cxpr_model_compiled* program,
                                                const cxpr_expr_ast* period_ast,
                                                size_t* out_capacity,
                                                cxpr_error* err) {
    double raw = 0.0;
    long period;
    (void)err;
    if (!period_ast || !out_capacity) return false;
    if (cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_expr_ast_param_name(period_ast);
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_constant_for_name(program, name);
        if (binding && binding->has_max_value && isfinite(binding->max_value)) {
            raw = binding->max_value;
            goto resolved;
        }
    }
    if (!cxpr_model_c_constant_param_expr(program, period_ast, &raw)) raw = 512.0;
resolved:
    if (!isfinite(raw) || raw < 1.0) raw = 1.0;
    period = lround(raw);
    if (period < 1) period = 1;
    *out_capacity = (size_t)period;
    return true;
}

static bool cxpr_model_c_period_ast_same(const cxpr_expr_ast* left, const cxpr_expr_ast* right) {
    if (!left || !right || cxpr_expr_ast_kind_of(left) != cxpr_expr_ast_kind_of(right)) return false;
    if (cxpr_expr_ast_kind_of(left) == CXPR_NODE_VARIABLE) {
        return cxpr_model_names_match(cxpr_expr_ast_param_name(left), cxpr_expr_ast_param_name(right));
    }
    if (cxpr_expr_ast_kind_of(left) == CXPR_NODE_NUMBER) {
        return fabs(cxpr_expr_ast_number_value(left) - cxpr_expr_ast_number_value(right)) < 1e-12;
    }
    return false;
}

static bool cxpr_model_c_ast_same_simple(const cxpr_expr_ast* left, const cxpr_expr_ast* right) {
    if (!left || !right || cxpr_expr_ast_kind_of(left) != cxpr_expr_ast_kind_of(right)) return false;
    switch (cxpr_expr_ast_kind_of(left)) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_names_match(cxpr_expr_ast_identifier_name(left),
                                      cxpr_expr_ast_identifier_name(right));
    case CXPR_NODE_VARIABLE:
        return cxpr_model_names_match(cxpr_expr_ast_param_name(left),
                                      cxpr_expr_ast_param_name(right));
    case CXPR_NODE_NUMBER:
        return fabs(cxpr_expr_ast_number_value(left) - cxpr_expr_ast_number_value(right)) < 1e-12;
    default:
        return false;
    }
}

const char* cxpr_model_c_find_common_binding_expr(const cxpr_model_compiled* program,
                                                         size_t binding_index,
                                                         const bool* needed_bindings,
                                                         const bool* skip_bindings,
                                                         char* const* emitted_names) {
    const cxpr_expr_ast* ast;
    if (!program || binding_index >= program->binding_count || !emitted_names) return NULL;
    if (program->bindings[binding_index].kind == CXPR_MODEL_BINDING_STATE_UPDATE) return NULL;
    ast = program->bindings[binding_index].ast;
    if (!ast) return NULL;
    (void)skip_bindings;
    for (size_t i = 0u; i < binding_index; ++i) {
        if (!needed_bindings[i] || !emitted_names[i]) continue;
        if (program->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (program->bindings[i].ast &&
            cxpr_model_ast_equal(program->bindings[i].ast, ast)) {
            return emitted_names[i];
        }
    }
    return NULL;
}

static bool cxpr_model_c_ast_is_number(const cxpr_expr_ast* ast, double value) {
    return ast &&
           cxpr_expr_ast_kind_of(ast) == CXPR_NODE_NUMBER &&
           fabs(cxpr_expr_ast_number_value(ast) - value) < 1e-12;
}

static bool cxpr_model_c_match_high_low_midpoint(const cxpr_expr_ast* ast,
                                                 const cxpr_expr_ast** out_high_ast,
                                                 const cxpr_expr_ast** out_low_ast,
                                                 const cxpr_expr_ast** out_period_ast) {
    const cxpr_expr_ast* left;
    const cxpr_expr_ast* right;
    const cxpr_expr_ast* high_call = NULL;
    const cxpr_expr_ast* low_call = NULL;
    const cxpr_expr_ast* high_period;
    const cxpr_expr_ast* low_period;

    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_expr_ast_operator(ast) != CXPR_TOK_PLUS) {
        return false;
    }
    left = cxpr_expr_ast_binary_left(ast);
    right = cxpr_expr_ast_binary_right(ast);
    if (left && cxpr_expr_ast_kind_of(left) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(left), "__cxpr_window_highest")) {
        high_call = left;
    } else if (left && cxpr_expr_ast_kind_of(left) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_expr_ast_call_name(left), "__cxpr_window_lowest")) {
        low_call = left;
    }
    if (right && cxpr_expr_ast_kind_of(right) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(right), "__cxpr_window_highest")) {
        high_call = right;
    } else if (right && cxpr_expr_ast_kind_of(right) == CXPR_NODE_FUNCTION_CALL &&
               cxpr_model_names_match(cxpr_expr_ast_call_name(right), "__cxpr_window_lowest")) {
        low_call = right;
    }
    if (!high_call || !low_call ||
        cxpr_expr_ast_call_arg_count(high_call) != 2u ||
        cxpr_expr_ast_call_arg_count(low_call) != 2u) {
        return false;
    }
    high_period = cxpr_expr_ast_call_arg(high_call, 1u);
    low_period = cxpr_expr_ast_call_arg(low_call, 1u);
    if (!cxpr_model_c_period_ast_same(high_period, low_period)) return false;
    if (out_high_ast) *out_high_ast = cxpr_expr_ast_call_arg(high_call, 0u);
    if (out_low_ast) *out_low_ast = cxpr_expr_ast_call_arg(low_call, 0u);
    if (out_period_ast) *out_period_ast = high_period;
    return true;
}

bool cxpr_model_c_match_scaled_high_low_midpoint(const cxpr_expr_ast* ast,
                                                        const cxpr_expr_ast** out_high_ast,
                                                        const cxpr_expr_ast** out_low_ast,
                                                        const cxpr_expr_ast** out_period_ast) {
    const cxpr_expr_ast* sum = NULL;
    if (out_high_ast) *out_high_ast = NULL;
    if (out_low_ast) *out_low_ast = NULL;
    if (out_period_ast) *out_period_ast = NULL;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_BINARY_OP ||
        cxpr_expr_ast_operator(ast) != CXPR_TOK_STAR) {
        return false;
    }
    if (cxpr_model_c_ast_is_number(cxpr_expr_ast_binary_left(ast), 0.5)) {
        sum = cxpr_expr_ast_binary_right(ast);
    } else if (cxpr_model_c_ast_is_number(cxpr_expr_ast_binary_right(ast), 0.5)) {
        sum = cxpr_expr_ast_binary_left(ast);
    }
    return sum && cxpr_model_c_match_high_low_midpoint(
                      sum, out_high_ast, out_low_ast, out_period_ast);
}

bool cxpr_model_c_emit_midpoint_binding(cxpr_model_c_buf* b,
                                               const char* name,
                                               const cxpr_expr_ast* high_ast,
                                               const cxpr_expr_ast* low_ast,
                                               const cxpr_expr_ast* period_ast,
                                               const cxpr_c_target* target,
                                               const cxpr_model_compiled* program,
                                               cxpr_error* err) {
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !high_ast || !low_ast || !period_ast || !target || !program) return false;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_highest = 0.0; double _cx_lowest = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_hi", high_ast, "_cx_i", target, program, err) &&
            cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_lo", low_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
                name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* high_expr = cxpr_expr_ast_to_c_at_offset(high_ast, (unsigned)i, target, err);
        char* low_expr = high_expr
                             ? cxpr_expr_ast_to_c_at_offset(low_ast, (unsigned)i, target, err)
                             : NULL;
        if (!high_expr || !low_expr) {
            free(high_expr);
            free(low_expr);
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_hi = %s; double _cx_lo = %s; if (!isnan(_cx_hi) && !isnan(_cx_lo)) { if (_cx_count == 0u) { _cx_highest = _cx_hi; _cx_lowest = _cx_lo; } if (_cx_hi > _cx_highest) _cx_highest = _cx_hi; if (_cx_lo < _cx_lowest) _cx_lowest = _cx_lo; _cx_count++; } }\n",
            i,
            high_expr,
            low_expr);
        free(high_expr);
        free(low_expr);
    }
    cxpr_model_c_printf(
        b,
        "        %s = _cx_count == 0u ? 0.0 : (_cx_highest + _cx_lowest) * 0.5; }\n",
        name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static int cxpr_model_c_window_op_code(const char* name) {
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    return window ? (int)window->reduction : -1;
}

bool cxpr_model_c_emit_simple_window_binding(cxpr_model_c_buf* b,
                                                    const char* name,
                                                    const cxpr_expr_ast* ast,
                                                    const cxpr_c_target* target,
                                                    const cxpr_model_compiled* program,
                                                    cxpr_error* err) {
    const char* fn_name;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    int op;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !name || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(ast) != 2u || !target || !program) {
        return false;
    }
    fn_name = cxpr_expr_ast_call_name(ast);
    op = cxpr_model_c_window_op_code(fn_name);
    if (op < 0) return false;
    value_ast = cxpr_expr_ast_call_arg(ast, 0u);
    if (cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_window_is_function(cxpr_expr_ast_call_name(value_ast))) {
        return false;
    }
    period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; double _cx_weighted_sum = 0.0; double _cx_weight_sum = 0.0; double _cx_extreme = 0.0; size_t _cx_count = 0u;\n",
        name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_printf(
                &loop,
                "            if (!isnan(_cx_x)) { double _cx_weight = (double)(_cx_limit - _cx_i); if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_weighted_sum += _cx_x * _cx_weight; _cx_weight_sum += _cx_weight; _cx_count++; }\n"
                "        }\n",
                op,
                op);
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            if (op == 2 || op == 3) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
            } else if (op == 1) {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
            } else if (op == 4) {
                cxpr_model_c_printf(
                    b,
                    "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                    name,
                    name);
            } else if (op == 5) {
                cxpr_model_c_printf(
                    b,
                    "        %s = _cx_weight_sum > 0.0 ? _cx_weighted_sum / _cx_weight_sum : 0.0; }\n",
                    name);
            } else {
                cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
            }
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_expr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { double _cx_weight = (double)(_cx_limit - %zuu); if (_cx_count == 0u) _cx_extreme = _cx_x; if (%d == 2 && _cx_x > _cx_extreme) _cx_extreme = _cx_x; if (%d == 3 && _cx_x < _cx_extreme) _cx_extreme = _cx_x; _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_weighted_sum += _cx_x * _cx_weight; _cx_weight_sum += _cx_weight; _cx_count++; } }\n",
            i,
            value_expr,
            i,
            op,
            op);
        free(value_expr);
    }
    if (op == 2 || op == 3) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_extreme; }\n", name);
    } else if (op == 1) {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum / (double)_cx_count; }\n", name);
    } else if (op == 4) {
        cxpr_model_c_printf(
            b,
            "        if (_cx_count == 0u) %s = 0.0; else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
            name,
            name);
    } else if (op == 5) {
        cxpr_model_c_printf(
            b,
            "        %s = _cx_weight_sum > 0.0 ? _cx_weighted_sum / _cx_weight_sum : 0.0; }\n",
            name);
    } else {
        cxpr_model_c_printf(b, "        %s = _cx_count == 0u ? 0.0 : _cx_sum; }\n", name);
    }
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_match_mean_stddev_pair(const cxpr_expr_ast* mean_ast,
                                                const cxpr_expr_ast* stddev_ast) {
    if (!mean_ast || !stddev_ast ||
        cxpr_expr_ast_kind_of(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_kind_of(stddev_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(mean_ast) != 2u ||
        cxpr_expr_ast_call_arg_count(stddev_ast) != 2u ||
        !cxpr_model_names_match(cxpr_expr_ast_call_name(mean_ast), "__cxpr_window_mean") ||
        !cxpr_model_names_match(cxpr_expr_ast_call_name(stddev_ast), "__cxpr_window_stddev")) {
        return false;
    }
    return cxpr_model_c_ast_same_simple(cxpr_expr_ast_call_arg(mean_ast, 0u),
                                        cxpr_expr_ast_call_arg(stddev_ast, 0u)) &&
           cxpr_model_c_period_ast_same(cxpr_expr_ast_call_arg(mean_ast, 1u),
                                        cxpr_expr_ast_call_arg(stddev_ast, 1u));
}

bool cxpr_model_c_emit_mean_stddev_bindings(cxpr_model_c_buf* b,
                                                   const char* mean_name,
                                                   const char* stddev_name,
                                                   const cxpr_expr_ast* mean_ast,
                                                   const cxpr_c_target* target,
                                                   const cxpr_model_compiled* program,
                                                   cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity = 0u;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;

    if (!b || !mean_name || !stddev_name || !mean_ast ||
        cxpr_expr_ast_kind_of(mean_ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(mean_ast) != 2u || !target || !program) {
        return false;
    }
    value_ast = cxpr_expr_ast_call_arg(mean_ast, 0u);
    period_ast = cxpr_expr_ast_call_arg(mean_ast, 1u);
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return false;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return false;
    {
        cxpr_model_c_buf pb = {0};
        cxpr_model_c_printf(
            &pb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            capacity,
            period_expr);
        if (pb.oom) {
            free(period_expr);
            free(pb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        period_limit_expr = pb.data;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; double %s; { const size_t _cx_limit = (size_t)(%s); double _cx_sum = 0.0; double _cx_sumsq = 0.0; size_t _cx_count = 0u;\n",
        mean_name,
        stddev_name,
        period_limit_expr);
    {
        cxpr_model_c_buf loop = {0};
        cxpr_model_c_puts(&loop, "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
        if (cxpr_model_c_emit_dynamic_history_value(
                &loop, "_cx_x", value_ast, "_cx_i", target, program, err)) {
            cxpr_model_c_puts(
                &loop,
                "            if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; }\n"
                "        }\n");
            if (loop.oom) {
                free(period_expr);
                free(period_limit_expr);
                free(loop.data);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_puts(b, loop.data);
            free(loop.data);
            cxpr_model_c_printf(
                b,
                "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
                mean_name,
                stddev_name,
                mean_name,
                stddev_name);
            free(period_expr);
            free(period_limit_expr);
            if (b->oom) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            return true;
        }
        if (err && err->code != CXPR_OK) {
            free(period_expr);
            free(period_limit_expr);
            free(loop.data);
            return false;
        }
        free(loop.data);
    }
    for (size_t i = 0u; i < capacity; ++i) {
        char* value_expr = cxpr_expr_ast_to_c_at_offset(value_ast, (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "        if (%zuu < _cx_limit) { double _cx_x = %s; if (!isnan(_cx_x)) { _cx_sum += _cx_x; _cx_sumsq += _cx_x * _cx_x; _cx_count++; } }\n",
            i,
            value_expr);
        free(value_expr);
    }
    cxpr_model_c_printf(
        b,
        "        if (_cx_count == 0u) { %s = 0.0; %s = 0.0; } else { double _cx_mean = _cx_sum / (double)_cx_count; double _cx_var = (_cx_sumsq / (double)_cx_count) - _cx_mean * _cx_mean; %s = _cx_mean; %s = sqrt(_cx_var > 0.0 ? _cx_var : 0.0); } }\n",
        mean_name,
        stddev_name,
        mean_name,
        stddev_name);
    free(period_expr);
    free(period_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

static size_t cxpr_model_c_standard_slot_count_inline(const cxpr_model_compiled* program) {
    (void)program;
    return 0u;
}

size_t cxpr_model_c_window_plan_base(const cxpr_model_compiled* program,
                                     const cxpr_model_window_plan_node* node) {
    if (!program || !node || node->slot_count == 0u) return (size_t)-1;
    return cxpr_model_c_standard_slot_count_inline(program) + node->slot_offset;
}

const char* cxpr_model_c_window_counter_type(
    const cxpr_model_window_plan_node* node) {
    if (!node || node->period_capacity > 255u) return "size_t";
    return "uint8_t";
}
