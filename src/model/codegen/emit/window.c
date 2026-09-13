#include "model/codegen/ast/internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool cxpr_model_c_period_default_value(const cxpr_model_compiled* program,
                                              const cxpr_expr_ast* period_ast,
                                              double* out_value) {
    return cxpr_model_c_constant_param_expr(program, period_ast, out_value);
}

static bool cxpr_model_c_period_is_static_capacity(const cxpr_model_compiled* program,
                                                   const cxpr_expr_ast* period_ast,
                                                   size_t capacity,
                                                   const cxpr_c_target* target) {
    const cxpr_model_ast_c_target* target_data =
        target ? (const cxpr_model_ast_c_target*)target->userdata : NULL;
    double value = 0.0;
    long rounded;

    if (!program || !period_ast || capacity == 0u) return false;
    if (cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE &&
        target_data &&
        target_data->literal_param_values) {
        const char* name = cxpr_expr_ast_param_name(period_ast);
        size_t index = cxpr_model_compiled_param_index(program, name);
        if (index != (size_t)-1 && index < target_data->literal_param_count) {
            value = target_data->literal_param_values[index];
        } else {
            return false;
        }
    } else if (!cxpr_model_c_period_default_value(program, period_ast, &value) ||
               cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE) {
        return false;
    }
    if (!isfinite(value) || value < 1.0) value = 1.0;
    rounded = lround(value);
    if (rounded < 1) rounded = 1;
    return (size_t)rounded == capacity;
}

static char* cxpr_model_c_period_limit_expr(const cxpr_model_compiled* program,
                                            const cxpr_expr_ast* period_ast,
                                            size_t capacity,
                                            const cxpr_c_target* target,
                                            cxpr_error* err) {
    const cxpr_model_ast_c_target* target_data =
        target ? (const cxpr_model_ast_c_target*)target->userdata : NULL;
    double default_value = 0.0;
    long rounded;
    char default_raw[64];
    char* period_expr;
    cxpr_model_c_buf b = {0};

    if (!program || !period_ast || !target || capacity == 0u) return NULL;
    if (cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE &&
        target_data &&
        target_data->literal_param_values) {
        const char* name = cxpr_expr_ast_param_name(period_ast);
        size_t index = cxpr_model_compiled_param_index(program, name);
        if (index != (size_t)-1 && index < target_data->literal_param_count) {
            double literal = target_data->literal_param_values[index];
            if (!isfinite(literal) || literal < 1.0) literal = 1.0;
            rounded = lround(literal);
            if (rounded < 1) rounded = 1;
            if ((size_t)rounded == capacity) {
                cxpr_model_c_printf(&b, "%zuu", capacity);
                return b.oom ? NULL : b.data;
            }
        }
    }
    if (cxpr_model_c_period_default_value(program, period_ast, &default_value) &&
        isfinite(default_value)) {
        rounded = lround(default_value < 1.0 ? 1.0 : default_value);
        if (rounded < 1) rounded = 1;
        if ((size_t)rounded == capacity) {
            if (cxpr_expr_ast_kind_of(period_ast) != CXPR_NODE_VARIABLE) {
                cxpr_model_c_printf(&b, "%zuu", capacity);
                return b.oom ? NULL : b.data;
            }
            cxpr_model_c_format_double(default_raw, sizeof(default_raw), default_value);
            period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
            if (!period_expr) return NULL;
            cxpr_model_c_printf(
                &b,
                "((%s) == %s ? %zuu : (size_t)((int)fmax(1.0, fmin((double)%zuu, round(%s)))))",
                period_expr,
                default_raw,
                capacity,
                capacity,
                period_expr);
            free(period_expr);
            return b.oom ? NULL : b.data;
        }
    }

    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return NULL;
    cxpr_model_c_printf(
        &b,
        "(size_t)((int)fmax(1.0, fmin((double)%zuu, round(%s))))",
        capacity,
        period_expr);
    free(period_expr);
    return b.oom ? NULL : b.data;
}

bool cxpr_model_c_emit_planned_roc_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err) {
    const cxpr_model_window_plan_node* roc_node;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* roc_period_ast;
    const cxpr_expr_ast* aggregate_period_ast;
    size_t roc_capacity;
    size_t aggregate_capacity;
    size_t extra_base;
    size_t node_index;
    char* roc_limit_expr = NULL;
    char* aggregate_limit_expr = NULL;
    bool static_roc;
    bool static_aggregate;

    if (!b || !name || !plan || !node || !target || !program ||
        !node->has_child ||
        (node->op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
         node->op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) ||
        node->child_index >= plan->node_count) {
        return false;
    }
    roc_node = &plan->nodes[node->child_index];
    if (roc_node->op != CXPR_MODEL_WINDOW_PLAN_OP_ROC) return false;
    value_ast = roc_node->value_ast;
    roc_period_ast = roc_node->period_ast;
    aggregate_period_ast = node->period_ast;
    roc_capacity = roc_node->period_capacity;
    aggregate_capacity = node->period_capacity;
    extra_base = cxpr_model_c_window_plan_base(program, node);
    if (extra_base == (size_t)-1 || aggregate_capacity == 0u) return false;
    node_index = (size_t)(node - plan->nodes);
    static_roc = cxpr_model_c_period_is_static_capacity(
        program, roc_period_ast, roc_capacity, target);
    static_aggregate = cxpr_model_c_period_is_static_capacity(
        program, aggregate_period_ast, aggregate_capacity, target);

    if (static_roc && static_aggregate) {
        char* now_expr = cxpr_expr_ast_to_c_at_offset(value_ast, 0u, target, err);
        char* prev_expr = now_expr
                              ? cxpr_expr_ast_to_c_at_offset(
                                    value_ast, (unsigned)roc_capacity, target, err)
                              : NULL;
        if (!now_expr || !prev_expr) {
            free(now_expr);
            free(prev_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "    double %s;\n"
            "    { size_t _cx_next = (size_t)_cx_state->window_%zu.next; size_t _cx_count = (size_t)_cx_state->window_%zu.count; double _cx_sum = _cx_state->window_%zu.sum; double _cx_now = %s; double _cx_prev = %s; double _cx_roc = isnan(_cx_now) ? NAN : ((isnan(_cx_prev) || fabs(_cx_prev) <= 1e-12) ? 0.0 : ((_cx_now - _cx_prev) / _cx_prev) * 100.0); double _cx_old = _cx_state->window_%zu.values[_cx_next]; if (!isnan(_cx_old)) { _cx_sum -= _cx_old; if (_cx_count > 0u) _cx_count--; } if (!isnan(_cx_roc)) { _cx_sum += _cx_roc; _cx_count++; } _cx_state->window_%zu.values[_cx_next] = _cx_roc; _cx_state->window_%zu.next = (%s)((_cx_next + 1u) %% %zuu); _cx_state->window_%zu.count = (%s)_cx_count; _cx_state->window_%zu.sum = _cx_sum; %s = _cx_count == 0u ? 0.0 : %s; }\n",
            name,
            node_index,
            node_index,
            node_index,
            now_expr,
            prev_expr,
            node_index,
            node_index,
            node_index,
            cxpr_model_c_window_counter_type(node),
            aggregate_capacity,
            node_index,
            cxpr_model_c_window_counter_type(node),
            node_index,
            name,
            node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN ? "_cx_sum / (double)_cx_count" : "_cx_sum");
        free(now_expr);
        free(prev_expr);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        return true;
    }

    roc_limit_expr = cxpr_model_c_period_limit_expr(
        program, roc_period_ast, roc_capacity, target, err);
    aggregate_limit_expr = roc_limit_expr
                               ? cxpr_model_c_period_limit_expr(
                                     program, aggregate_period_ast, aggregate_capacity, target, err)
                               : NULL;
    if (!roc_limit_expr || !aggregate_limit_expr) {
        free(roc_limit_expr);
        free(aggregate_limit_expr);
        return false;
    }

    {
        char* now_expr = cxpr_expr_ast_to_c_at_offset(value_ast, 0u, target, err);
        char* prev_expr = now_expr
                              ? cxpr_expr_ast_to_c_at_offset(
                                    value_ast, (unsigned)roc_capacity, target, err)
                              : NULL;
        if (!now_expr || !prev_expr) {
            free(now_expr);
            free(prev_expr);
            free(roc_limit_expr);
            free(aggregate_limit_expr);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "    double %s; { const size_t _cx_rp = (size_t)(%s); const size_t _cx_ap = (size_t)(%s);\n",
            name,
            roc_limit_expr,
            aggregate_limit_expr);
        if (!cxpr_model_c_emit_planned_roc_rolling_update(
                b,
                name,
                node,
                node_index,
                roc_capacity,
                aggregate_capacity,
                cxpr_model_c_window_counter_type(node),
                now_expr,
                prev_expr,
                err)) {
            free(now_expr);
            free(prev_expr);
            free(roc_limit_expr);
            free(aggregate_limit_expr);
            return false;
        }
        free(now_expr);
        free(prev_expr);
    }

    if (!cxpr_model_c_emit_planned_roc_aggregate_fallback(
            b, name, value_ast, node->op, target, program, err)) {
        free(roc_limit_expr);
        free(aggregate_limit_expr);
        return false;
    }
    cxpr_model_c_puts(b, "        } } }\n");
    free(roc_limit_expr);
    free(aggregate_limit_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_emit_planned_simple_aggregate_binding(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan* plan,
    const cxpr_model_window_plan_node* node,
    const cxpr_c_target* target,
    const cxpr_model_compiled* program,
    cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity;
    size_t node_index;
    char* period_limit_expr = NULL;
    char* value_expr = NULL;
    cxpr_model_c_buf fallback = {0};
    bool static_period;

    if (!b || !name || !plan || !node || !target || !program ||
        node->has_child ||
        (node->op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
         node->op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) ||
        node->period_capacity == 0u) {
        return false;
    }
    value_ast = node->value_ast;
    period_ast = node->period_ast;
    capacity = node->period_capacity;
    node_index = (size_t)(node - plan->nodes);
    static_period = cxpr_model_c_period_is_static_capacity(
        program, period_ast, capacity, target);

    period_limit_expr = cxpr_model_c_period_limit_expr(
        program, period_ast, capacity, target, err);
    value_expr = period_limit_expr ? cxpr_expr_ast_to_c_at_offset(value_ast, 0u, target, err) : NULL;
    if (!period_limit_expr || !value_expr) {
        free(period_limit_expr);
        free(value_expr);
        return false;
    }

    cxpr_model_c_puts(
        &fallback,
        "            { double _cx_fallback_sum = 0.0; size_t _cx_fallback_count = 0u;\n"
        "        for (size_t _cx_i = 0u; _cx_i < _cx_limit; ++_cx_i) {\n");
    if (!cxpr_model_c_emit_dynamic_history_value(
            &fallback, "_cx_x", value_ast, "_cx_i", target, program, err)) {
        free(fallback.data);
        free(period_limit_expr);
        free(value_expr);
        return false;
    }
    cxpr_model_c_puts(
        &fallback,
        "            if (!isnan(_cx_x)) { _cx_fallback_sum += _cx_x; _cx_fallback_count++; }\n"
        "        }\n");
    cxpr_model_c_printf(
        &fallback,
        "        %s = _cx_fallback_count == 0u ? 0.0 : %s; }\n",
        name,
        node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN
            ? "_cx_fallback_sum / (double)_cx_fallback_count"
            : "_cx_fallback_sum");
    if (fallback.oom) {
        free(fallback.data);
        free(period_limit_expr);
        free(value_expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }

    if (static_period) {
        cxpr_model_c_printf(
            b,
            "    double %s;\n"
            "    { size_t _cx_next = (size_t)_cx_state->window_%zu.next; size_t _cx_count = (size_t)_cx_state->window_%zu.count; double _cx_sum = _cx_state->window_%zu.sum; double _cx_value = %s; double _cx_old = _cx_state->window_%zu.values[_cx_next]; if (!isnan(_cx_old)) { _cx_sum -= _cx_old; if (_cx_count > 0u) _cx_count--; } if (!isnan(_cx_value)) { _cx_sum += _cx_value; _cx_count++; } _cx_state->window_%zu.values[_cx_next] = _cx_value; _cx_state->window_%zu.next = (%s)((_cx_next + 1u) %% %zuu); _cx_state->window_%zu.count = (%s)_cx_count; _cx_state->window_%zu.sum = _cx_sum; %s = _cx_count == 0u ? 0.0 : %s; }\n",
            name,
            node_index,
            node_index,
            node_index,
            value_expr,
            node_index,
            node_index,
            node_index,
            cxpr_model_c_window_counter_type(node),
            capacity,
            node_index,
            cxpr_model_c_window_counter_type(node),
            node_index,
            name,
            node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN ? "_cx_sum / (double)_cx_count" : "_cx_sum");
        free(fallback.data);
        free(period_limit_expr);
        free(value_expr);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        return true;
    }

    cxpr_model_c_printf(
        b,
        "    double %s; { const size_t _cx_limit = (size_t)(%s);\n"
        "        { size_t _cx_next = (size_t)_cx_state->window_%zu.next; size_t _cx_count = (size_t)_cx_state->window_%zu.count; double _cx_sum = _cx_state->window_%zu.sum; double _cx_value = %s; double _cx_old = _cx_state->window_%zu.values[_cx_next]; if (!isnan(_cx_old)) { _cx_sum -= _cx_old; if (_cx_count > 0u) _cx_count--; } if (!isnan(_cx_value)) { _cx_sum += _cx_value; _cx_count++; } _cx_state->window_%zu.values[_cx_next] = _cx_value; _cx_state->window_%zu.next = (%s)((_cx_next + 1u) %% %zuu); _cx_state->window_%zu.count = (%s)_cx_count; _cx_state->window_%zu.sum = _cx_sum; if (!CXPR_UNLIKELY(_cx_limit != %zuu)) { %s = _cx_count == 0u ? 0.0 : %s; } else {\n",
        name,
        period_limit_expr,
        node_index,
        node_index,
        node_index,
        value_expr,
        node_index,
        node_index,
        node_index,
        cxpr_model_c_window_counter_type(node),
        capacity,
        node_index,
        cxpr_model_c_window_counter_type(node),
        node_index,
        capacity,
        name,
        node->op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN ? "_cx_sum / (double)_cx_count" : "_cx_sum");
    cxpr_model_c_puts(b, fallback.data);
    cxpr_model_c_puts(b, "        } } }\n");
    free(fallback.data);
    free(period_limit_expr);
    free(value_expr);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

typedef bool (*cxpr_model_c_single_binding_emitter)(cxpr_model_c_buf* b,
                                                    const char* name,
                                                    const cxpr_expr_ast* ast,
                                                    const cxpr_c_target* target,
                                                    const cxpr_model_compiled* program,
                                                    cxpr_error* err);

static bool cxpr_model_c_emit_midpoint_binding_from_ast(cxpr_model_c_buf* b,
                                                        const char* name,
                                                        const cxpr_expr_ast* ast,
                                                        const cxpr_c_target* target,
                                                        const cxpr_model_compiled* program,
                                                        cxpr_error* err) {
    const cxpr_expr_ast* high_ast = NULL;
    const cxpr_expr_ast* low_ast = NULL;
    const cxpr_expr_ast* period_ast = NULL;
    if (!cxpr_model_c_match_scaled_high_low_midpoint(ast, &high_ast, &low_ast, &period_ast)) {
        return false;
    }
    return cxpr_model_c_emit_midpoint_binding(
        b, name, high_ast, low_ast, period_ast, target, program, err);
}

bool cxpr_model_c_emit_optimized_single_binding(cxpr_model_c_buf* b,
                                                       const char* name,
                                                       const cxpr_expr_ast* ast,
                                                       const cxpr_c_target* target,
                                                       const cxpr_model_compiled* program,
                                                       cxpr_error* err) {
    static const cxpr_model_c_single_binding_emitter emitters[] = {
        cxpr_model_c_emit_midpoint_binding_from_ast,
        cxpr_model_c_emit_simple_window_binding,
    };
    for (size_t i = 0u; i < sizeof(emitters) / sizeof(emitters[0]); ++i) {
        if (emitters[i](b, name, ast, target, program, err)) return true;
        if (err && err->code != CXPR_OK) return false;
    }
    return false;
}

bool cxpr_model_c_emit_optimized_binding_pair(cxpr_model_c_buf* b,
                                                     const char* first_name,
                                                     const char* second_name,
                                                     const cxpr_expr_ast* first_ast,
                                                     const cxpr_expr_ast* second_ast,
                                                     const cxpr_c_target* target,
                                                     const cxpr_model_compiled* program,
                                                     cxpr_error* err) {
    if (!cxpr_model_c_match_mean_stddev_pair(first_ast, second_ast)) return false;
    return cxpr_model_c_emit_mean_stddev_bindings(
        b, first_name, second_name, first_ast, target, program, err);
}

