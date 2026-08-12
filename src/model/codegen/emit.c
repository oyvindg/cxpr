#include "model/codegen/codegen_ast_internal.h"
#include "model/window/window.h"
#include "registry/internal.h"

#include <cxpr/resample.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* cxpr_model_ast_expr_to_c_with_temps(
    cxpr_model_ast_temp_emit* emit,
    const cxpr_expr_ast* ast,
    cxpr_error* err);
static bool cxpr_model_c_collect_defined_function_refs(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    bool* used,
    cxpr_error* err);

const char* cxpr_model_c_history_counter_type(size_t capacity) {
    return capacity > 255u ? "size_t" : "uint8_t";
}

bool cxpr_model_c_emit_runtime_state_typedef(
    cxpr_model_c_buf* b,
    const cxpr_model_compiled* program,
    const cxpr_model_window_plan* window_plan,
    const char* safe_name,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    cxpr_model_c_printf(b, "typedef struct %s_state {\n", safe_name);
    cxpr_model_c_puts(b, "    uint8_t init;\n");
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name("state_", program->state_defaults[i].name);
        if (!field_name) {
            free(field_name);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "    %s %s;\n",
            program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_BOOL
                ? "uint8_t"
                : "double",
            field_name);
        free(field_name);
    }
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t capacity;
        if (program->history_specs[i].depth == 0u) continue;
        capacity = cxpr_model_c_history_capacity(program->history_specs[i].depth);
        cxpr_model_c_printf(b, "    cxpr_history%zu history_%zu;\n", capacity, i);
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            if (node->slot_count < 4u) continue;
            cxpr_model_c_printf(b, "    cxpr_window%zu window_%zu;\n",
                                node->slot_count - 4u, i);
        }
    }
    for (size_t i = 0u; i < child_call_count; ++i) {
        size_t child_index = child_call_child_indices ? child_call_child_indices[i] : (size_t)-1;
        char* child_tick_name;
        if (child_index >= program->child_count) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid child model callsite", 0, 0);
            return false;
        }
        child_tick_name = cxpr_model_c_child_tick_name(safe_name, child_index);
        if (!child_tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "    %s_state child_call_%zu_state;\n", child_tick_name, i);
        cxpr_model_c_printf(b, "    uint8_t child_call_%zu_initialized;\n", i);
        cxpr_model_c_printf(b, "    double child_call_%zu_outputs[%zu];\n",
                            i,
                            program->children[child_index].program &&
                                    program->children[child_index].program->output_count
                                ? program->children[child_index].program->output_count
                                : 1u);
        free(child_tick_name);
    }
    cxpr_model_c_printf(b, "} %s_state;\n\n", safe_name);
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_emit_state_typedefs(cxpr_model_c_buf* b,
                                             const cxpr_model_compiled* program,
                                             const cxpr_model_window_plan* window_plan,
                                             const char* safe_name,
                                             cxpr_error* err) {
    if (!b || !program || !safe_name) return false;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        const char* counter_type = cxpr_model_c_history_counter_type(capacity);
        bool already_emitted = false;
        if (depth == 0u) continue;
        for (size_t j = 0u; j < i; ++j) {
            if (program->history_specs[j].depth > 0u &&
                cxpr_model_c_history_capacity(program->history_specs[j].depth) == capacity) {
                already_emitted = true;
                break;
            }
        }
        if (already_emitted) continue;
        if (cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_HISTORY%zu_DEFINED\n"
                "#define CXPR_HISTORY%zu_DEFINED\n"
                "typedef struct { double values[%zu]; } cxpr_history%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                capacity,
                capacity);
        } else {
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_HISTORY%zu_DEFINED\n"
                "#define CXPR_HISTORY%zu_DEFINED\n"
                "typedef struct { %s next; double values[%zu]; } cxpr_history%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                counter_type,
                capacity,
                capacity);
        }
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            const char* counter_type = cxpr_model_c_window_counter_type(node);
            size_t capacity = node->slot_count >= 4u ? node->slot_count - 4u : 0u;
            bool already_emitted = false;
            if (node->slot_count < 4u) continue;
            for (size_t j = 0u; j < i; ++j) {
                const cxpr_model_window_plan_node* prior = &window_plan->nodes[j];
                if (prior->slot_count >= 4u && prior->slot_count - 4u == capacity) {
                    already_emitted = true;
                    break;
                }
            }
            if (already_emitted) continue;
            cxpr_model_c_printf(
                b,
                "#ifndef CXPR_WINDOW%zu_DEFINED\n"
                "#define CXPR_WINDOW%zu_DEFINED\n"
                "typedef struct { uint8_t init; %s next; %s count; double sum; double values[%zu]; } cxpr_window%zu;\n"
                "#endif\n",
                capacity,
                capacity,
                counter_type,
                counter_type,
                capacity,
                capacity);
        }
    }
    if ((program->history_spec_count > 0u || (window_plan && window_plan->node_count > 0u))) {
        cxpr_model_c_puts(b, "\n");
    }
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_init_sentinel_slot(const cxpr_model_compiled* program,
                                            const cxpr_model_window_plan* window_plan,
                                            size_t* out_slot) {
    (void)out_slot;
    if (!program) return false;
    if (program->state_default_count > 0u) return true;
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        if (program->history_specs[i].depth > 0u) return true;
    }
    return window_plan && window_plan->node_count > 0u;
}

bool cxpr_model_c_emit_slot_init_function(cxpr_model_c_buf* b,
                                                 const cxpr_model_compiled* program,
                                                 const cxpr_model_window_plan* window_plan,
                                                 const char* qualifiers,
                                                 const char* safe_name,
                                                 cxpr_error* err) {
    size_t sentinel = 0u;
    if (!b || !program || !safe_name) return false;
    if (!cxpr_model_c_init_sentinel_slot(program, window_plan, &sentinel)) return true;
    (void)sentinel;
    cxpr_model_c_printf(b, "/* Source model slot init: %s */\n", safe_name);
    if (qualifiers && qualifiers[0]) cxpr_model_c_printf(b, "%s ", qualifiers);
    cxpr_model_c_printf(
        b,
        "void %s_init_state(%s_state* restrict _cx_state) {\n",
        safe_name,
        safe_name);
    for (size_t i = 0u; i < program->history_spec_count; ++i) {
        size_t depth = program->history_specs[i].depth;
        size_t capacity = cxpr_model_c_history_capacity(depth);
        if (depth == 0u) continue;
        cxpr_model_c_printf(
            b,
            "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->history_%zu.values[_cx_init_i] = NAN;\n",
            capacity,
            i);
        if (!cxpr_model_c_history_use_shift(depth)) {
            cxpr_model_c_printf(b, "    _cx_state->history_%zu.next = 0u;\n", i);
        }
    }
    if (window_plan) {
        for (size_t i = 0u; i < window_plan->node_count; ++i) {
            const cxpr_model_window_plan_node* node = &window_plan->nodes[i];
            size_t base = cxpr_model_c_window_plan_base(program, node);
            if (base == (size_t)-1 || node->slot_count < 4u) continue;
            (void)base;
            cxpr_model_c_printf(
                b,
                "    for (size_t _cx_init_i = 0u; _cx_init_i < %zuu; ++_cx_init_i) _cx_state->window_%zu.values[_cx_init_i] = NAN;\n"
                "    _cx_state->window_%zu.next = 0u;\n"
                "    _cx_state->window_%zu.count = 0u;\n"
                "    _cx_state->window_%zu.sum = 0.0;\n"
                "    _cx_state->window_%zu.init = 1u;\n",
                node->slot_count - 4u,
                i,
                i,
                i,
                i,
                i);
        }
    }
    for (size_t i = 0u; i < program->state_default_count; ++i) {
        char* field_name = cxpr_model_c_prefixed_name(
            "state_", program->state_defaults[i].name);
        if (!field_name) {
            free(field_name);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(
            b,
            "    _cx_state->%s = %s;\n",
            field_name,
            program->state_defaults[i].result_kind == CXPR_MODEL_RESULT_BOOL
                ? (cxpr_expr_ast_kind_of(program->state_defaults[i].ast) == CXPR_NODE_BOOL &&
                           cxpr_expr_ast_bool_value(program->state_defaults[i].ast)
                       ? "1u"
                       : "0u")
                : "0.0");
        free(field_name);
    }
    cxpr_model_c_puts(b, "    _cx_state->init = 1u;\n");
    cxpr_model_c_puts(b, "}\n\n");
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

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

static char* cxpr_model_ast_c_emit_window_call(const cxpr_expr_ast* ast,
                                               unsigned lookback_offset,
                                               const cxpr_c_target* target,
                                               const cxpr_model_compiled* program,
                                               cxpr_error* err) {
    const char* name = cxpr_expr_ast_call_name(ast);
    const char* op = cxpr_model_c_window_op(name);
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    bool is_roc = window && window->op == CXPR_WINDOW_OP_ROC;
    bool is_bars_since_extreme =
        window && window->op == CXPR_WINDOW_OP_BARS_SINCE_EXTREME;
    bool is_window_mean_absdev =
        window && window->op == CXPR_WINDOW_OP_MEAN_ABSDEV;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity = 0u;
    size_t value_count;
    char* period_expr = NULL;
    char* period_limit_expr = NULL;
    bool guard_values = false;
    cxpr_model_c_buf b = {0};
    if (!program || !window ||
        (!op && !is_roc && !is_bars_since_extreme && !is_window_mean_absdev) ||
        cxpr_expr_ast_call_arg_count(ast) != window->arity) {
        cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                             "window function has wrong arity", 0, 0);
        return NULL;
    }
    value_ast = cxpr_expr_ast_call_arg(ast, 0u);
    period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    guard_values = cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE;
    if (!cxpr_model_c_window_period_capacity(program, period_ast, &capacity, err)) return NULL;
    period_expr = cxpr_expr_ast_to_c_at_offset(period_ast, 0u, target, err);
    if (!period_expr) return NULL;
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
            return NULL;
        }
        period_limit_expr = pb.data;
    }
    if (cxpr_model_names_match(name, "__cxpr_window_mean") &&
        cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_names_match(cxpr_expr_ast_call_name(value_ast), "__cxpr_window_roc") &&
        cxpr_expr_ast_call_arg_count(value_ast) == 2u) {
        const cxpr_expr_ast* roc_value_ast = cxpr_expr_ast_call_arg(value_ast, 0u);
        const cxpr_expr_ast* roc_period_ast = cxpr_expr_ast_call_arg(value_ast, 1u);
        size_t roc_capacity = 0u;
        size_t source_count;
        char* roc_period_expr;
        char* roc_limit_expr;
        cxpr_model_c_buf rb = {0};
        if (!cxpr_model_c_window_period_capacity(program, roc_period_ast, &roc_capacity, err)) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        roc_period_expr = cxpr_expr_ast_to_c_at_offset(roc_period_ast, 0u, target, err);
        if (!roc_period_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_printf(
            &rb,
            "(int)fmax(1.0, fmin((double)%zuu, round(%s)))",
            roc_capacity,
            roc_period_expr);
        if (rb.oom) {
            free(roc_period_expr);
            free(period_expr);
            free(period_limit_expr);
            free(rb.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        roc_limit_expr = rb.data;
        source_count = capacity + roc_capacity;
        cxpr_model_c_puts(&b, "cxpr_model_window_mean_roc_c((const double[]){");
        for (size_t i = 0u; i < source_count; ++i) {
            char* source_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            source_expr = cxpr_expr_ast_to_c_at_offset(
                roc_value_ast, lookback_offset + (unsigned)i, target, err);
            if (!source_expr) {
                free(roc_period_expr);
                free(roc_limit_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            cxpr_model_c_printf(&b, "(%s)", source_expr);
            free(source_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            source_count,
            roc_limit_expr,
            period_limit_expr);
        free(roc_period_expr);
        free(roc_limit_expr);
        free(period_expr);
        free(period_limit_expr);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    if (is_bars_since_extreme) {
        const cxpr_expr_ast* mode_ast = cxpr_expr_ast_call_arg(ast, 2u);
        char* mode_expr = cxpr_expr_ast_to_c_at_offset(mode_ast, 0u, target, err);
        if (!mode_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_puts(&b, "cxpr_model_bars_since_extreme_c((const double[]){");
        for (size_t i = 0u; i < capacity; ++i) {
            char* value_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            value_expr = cxpr_expr_ast_to_c_at_offset(
                value_ast, lookback_offset + (unsigned)i, target, err);
            if (!value_expr) {
                free(mode_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            if (!guard_values) {
                cxpr_model_c_printf(&b, "(%s)", value_expr);
            } else {
                cxpr_model_c_printf(&b,
                                    "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                    i,
                                    period_limit_expr,
                                    value_expr);
            }
            free(value_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            mode_expr);
        free(mode_expr);
        free(period_expr);
        free(period_limit_expr);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    if (is_window_mean_absdev) {
        const cxpr_expr_ast* center_ast = cxpr_expr_ast_call_arg(ast, 2u);
        char* center_expr = cxpr_expr_ast_to_c_at_offset(center_ast, lookback_offset, target, err);
        if (!center_expr) {
            free(period_expr);
            free(period_limit_expr);
            return NULL;
        }
        cxpr_model_c_puts(&b, "cxpr_model_window_mean_absdev_c((const double[]){");
        for (size_t i = 0u; i < capacity; ++i) {
            char* value_expr;
            if (i > 0u) cxpr_model_c_puts(&b, ", ");
            value_expr = cxpr_expr_ast_to_c_at_offset(
                value_ast, lookback_offset + (unsigned)i, target, err);
            if (!value_expr) {
                free(center_expr);
                free(period_expr);
                free(period_limit_expr);
                free(b.data);
                return NULL;
            }
            if (!guard_values) {
                cxpr_model_c_printf(&b, "(%s)", value_expr);
            } else {
                cxpr_model_c_printf(&b,
                                    "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                    i,
                                    period_limit_expr,
                                    value_expr);
            }
            free(value_expr);
        }
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            center_expr);
        free(center_expr);
        free(period_expr);
        free(period_limit_expr);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    value_count = is_roc ? capacity + 1u : capacity;
    cxpr_model_c_puts(&b, is_roc
                          ? "cxpr_model_window_roc_c((const double[]){"
                          : "cxpr_model_window_eval_c((const double[]){");
    for (size_t i = 0u; i < value_count; ++i) {
        char* value_expr;
        if (i > 0u) cxpr_model_c_puts(&b, ", ");
        value_expr = (cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
                      cxpr_model_window_is_function(cxpr_expr_ast_call_name(value_ast)))
                         ? cxpr_model_ast_c_emit_window_call(
                               value_ast, lookback_offset + (unsigned)i, target, program, err)
                         : cxpr_expr_ast_to_c_at_offset(
                               value_ast, lookback_offset + (unsigned)i, target, err);
        if (!value_expr) {
            free(period_expr);
            free(period_limit_expr);
            free(b.data);
            return NULL;
        }
        if (!guard_values) {
            cxpr_model_c_printf(&b, "(%s)", value_expr);
        } else if (is_roc) {
            cxpr_model_c_printf(&b,
                                "((%zuu == 0u || %zuu <= (size_t)(%s)) ? (%s) : NAN)",
                                i,
                                i,
                                period_limit_expr,
                                value_expr);
        } else {
            cxpr_model_c_printf(&b,
                                "((%zuu < (size_t)(%s)) ? (%s) : NAN)",
                                i,
                                period_limit_expr,
                                value_expr);
        }
        free(value_expr);
    }
    if (is_roc) {
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s)",
            value_count,
            period_limit_expr);
    } else {
        cxpr_model_c_printf(
            &b,
            "}, %zuu, %s, %s)",
            capacity,
            period_limit_expr,
            op);
    }
    free(period_expr);
    free(period_limit_expr);
    if (b.oom) {
        free(b.data);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    return b.data;
}

char* cxpr_model_ast_c_emit_call(const cxpr_expr_ast* ast,
                                        unsigned lookback_offset,
                                        void* userdata,
                                        bool* handled,
                                        cxpr_error* err) {
    const char* name = cxpr_expr_ast_call_name(ast);
    size_t argc = cxpr_expr_ast_call_arg_count(ast);
    cxpr_model_ast_c_target* target_data = (cxpr_model_ast_c_target*)userdata;
    const cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = userdata,
    };
    cxpr_model_c_buf b = {0};

    (void)target_data;
    if (handled) *handled = false;
    if (!name) return NULL;

    if (cxpr_model_names_match(name, "resample")) {
        cxpr_resample_call call = {0};
        const char* source_name;
        size_t slot = (size_t)-1;
        char raw[768];
        if (handled) *handled = true;
        if (!target_data || !target_data->program ||
            !cxpr_resample_call_parse(ast, &call, err) || !call.source ||
            cxpr_expr_ast_kind_of(call.source) != CXPR_NODE_IDENTIFIER) return NULL;
        source_name = cxpr_expr_ast_identifier_name(call.source);
        for (size_t i = 0u; i < target_data->program->resample_requirement_count; ++i) {
            const cxpr_model_resample_requirement* req =
                &target_data->program->resample_requirements[i];
            if (req->duration_ns == call.every.duration_ns &&
                cxpr_model_names_match(req->source_name, source_name)) {
                slot = i;
                break;
            }
        }
        if (slot == (size_t)-1) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Unknown generated resample requirement", 0, 0);
            return NULL;
        }
        for (size_t i = 0u; i < target_data->resample_cse_count; ++i) {
            const cxpr_model_resample_cse* cse = &target_data->resample_cse[i];
            if (cse->uses >= 2u && cse->slot == slot &&
                cse->lookback == lookback_offset) {
                snprintf(raw, sizeof(raw), "_cx_resample_value_%zu_%u",
                         slot, lookback_offset);
                return cxpr_strdup(raw);
            }
        }
        snprintf(raw, sizeof(raw),
            "((_cx_primary_cursor < _cx_resample_views[%zu].primary_count && "
            "_cx_resample_views[%zu].values && "
            "_cx_resample_views[%zu].alignment && "
            "_cx_resample_views[%zu].alignment[_cx_primary_cursor] >= %uu && "
            "_cx_resample_views[%zu].alignment[_cx_primary_cursor] - %uu < "
            "_cx_resample_views[%zu].value_count) ? "
            "_cx_resample_views[%zu].values[_cx_resample_views[%zu].alignment[_cx_primary_cursor] - %uu] : NAN)",
            slot, slot, slot, slot, lookback_offset, slot, lookback_offset,
            slot, slot, slot, lookback_offset);
        return cxpr_strdup(raw);
    }

    if (target_data && target_data->program &&
        target_data->program->registry) {
        cxpr_func_entry* entry = cxpr_registry_find(
            target_data->program->registry, name);
        if (entry && entry->model_producer &&
            !cxpr_model_names_match(name, "abs") &&
            entry->defined_return_field_count == 1u) {
            cxpr_expr_ast producer = {0};
            producer.type = CXPR_NODE_PRODUCER_ACCESS;
            producer.data.producer_access.name = ast->data.function_call.name;
            producer.data.producer_access.args = ast->data.function_call.args;
            producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
            producer.data.producer_access.argc = ast->data.function_call.argc;
            producer.data.producer_access.field =
                entry->defined_return_field_names[0];
            if (handled) *handled = true;
            return cxpr_model_ast_producer_access_to_c(
                target_data->program,
                &producer,
                target_data->function_prefix,
                target_data->literal_param_values,
                target_data->literal_param_count,
                target_data->child_call_keys,
                target_data->child_call_child_indices,
                target_data->child_call_count,
                err);
        }
    }

    if (cxpr_model_window_is_function(name)) {
        if (handled) *handled = true;
        return cxpr_model_ast_c_emit_window_call(
            ast, lookback_offset, &target,
            target_data ? target_data->program : NULL,
            err);
    }

    if (cxpr_model_names_match(name, "roc") && argc == 2u) {
        char* current;
        char* previous;
        const cxpr_expr_ast* period = cxpr_expr_ast_call_arg(ast, 1u);
        double raw_period;
        unsigned period_offset;
        if (handled) *handled = true;
        if (!period || cxpr_expr_ast_kind_of(period) != CXPR_NODE_NUMBER) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "roc C codegen requires a constant period", 0, 0);
            return NULL;
        }
        raw_period = cxpr_expr_ast_number_value(period);
        period_offset = raw_period > 0.0 ? (unsigned)(raw_period + 0.5) : 0u;
        if (!isfinite(raw_period) || period_offset == 0u ||
            fabs(raw_period - (double)period_offset) > 1e-9) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "roc period must be a positive integer", 0, 0);
            return NULL;
        }
        current = cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 0u), lookback_offset, &target, err);
        previous = current ? cxpr_expr_ast_to_c_at_offset(
            cxpr_expr_ast_call_arg(ast, 0u), lookback_offset + period_offset,
            &target, err) : NULL;
        if (!current || !previous) {
            free(current);
            free(previous);
            return NULL;
        }
        cxpr_model_c_printf(
            &b,
            "((isnan(%s) || isnan(%s) || fabs(%s) <= 1e-12) ? 0.0 : (((%s) - (%s)) / (%s)) * 100.0)",
            current, previous, previous, current, previous, previous);
        free(current);
        free(previous);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }

    if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
        argc == 2u) {
        char* left;
        char* right;
        const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
        if (handled) *handled = true;
        left = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 0u),
                                       lookback_offset, &target, err);
        right = left ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 1u),
                                               lookback_offset, &target, err) : NULL;
        if (!left || !right) {
            free(left);
            free(right);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s %s %s) ? (%s) : (%s))",
                            left, op, right, left, right);
        free(left);
        free(right);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (cxpr_model_names_match(name, "mean") && argc >= 1u && argc <= 8u) {
        if (handled) *handled = true;
        cxpr_model_c_puts(&b, "((");
        for (size_t i = 0u; i < argc; ++i) {
            char* arg = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_call_arg(ast, i), lookback_offset, &target, err);
            if (!arg) {
                free(b.data);
                return NULL;
            }
            if (i > 0u) cxpr_model_c_puts(&b, " + ");
            cxpr_model_c_printf(&b, "(%s)", arg);
            free(arg);
        }
        cxpr_model_c_printf(&b, ") / %.1f)", (double)argc);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (cxpr_model_names_match(name, "if") && argc == 3u) {
        char* cond;
        char* yes;
        char* no;
        if (handled) *handled = true;
        cond = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 0u),
                                       lookback_offset, &target, err);
        yes = cond ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 1u),
                                             lookback_offset, &target, err) : NULL;
        no = yes ? cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, 2u),
                                           lookback_offset, &target, err) : NULL;
        if (!cond || !yes || !no) {
            free(cond);
            free(yes);
            free(no);
            return NULL;
        }
        cxpr_model_c_printf(&b, "((%s) ? (%s) : (%s))", cond, yes, no);
        free(cond);
        free(yes);
        free(no);
        if (b.oom) {
            free(b.data);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return NULL;
        }
        return b.data;
    }

    if (target_data && target_data->program && target_data->program->registry) {
        cxpr_func_entry* entry = cxpr_registry_find(target_data->program->registry, name);
        if (entry && entry->defined_body && entry->defined_return_field_count == 0u) {
            char* fn_name;
            if (entry->defined_param_count != argc) {
                if (err) {
                    err->code = CXPR_ERR_SYNTAX;
                    err->message = "Model function arity mismatch";
                }
                return NULL;
            }
            if (handled) *handled = true;
            if (target_data->inline_defined_functions) {
                char** names = NULL;
                char** exprs = NULL;
                cxpr_model_ast_c_target inline_data = *target_data;
                cxpr_c_target inline_target = {
                    .api_version = CXPR_C_TARGET_API_VERSION,
                    .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                    .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                    .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                    .userdata = &inline_data,
                };
                char* expr;
                names = (char**)calloc(argc ? argc : 1u, sizeof(char*));
                exprs = (char**)calloc(argc ? argc : 1u, sizeof(char*));
                if (!names || !exprs) {
                    free(names);
                    free(exprs);
                    if (err) {
                        err->code = CXPR_ERR_OUT_OF_MEMORY;
                        err->message = "Out of memory";
                    }
                    return NULL;
                }
                for (size_t i = 0u; i < argc; ++i) {
                    names[i] = entry->defined_param_names[i];
                    exprs[i] = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, i),
                                                       lookback_offset, &target, err);
                    if (!exprs[i]) {
                        for (size_t j = 0u; j < i; ++j) free(exprs[j]);
                        free(exprs);
                        free(names);
                        return NULL;
                    }
                }
                inline_data.param_names = names;
                inline_data.param_exprs = exprs;
                inline_data.param_count = argc;
                expr = cxpr_expr_ast_to_c_at_offset(entry->defined_body,
                                               lookback_offset,
                                               &inline_target,
                                               err);
                for (size_t i = 0u; i < argc; ++i) free(exprs[i]);
                free(exprs);
                free(names);
                if (!expr) return NULL;
                cxpr_model_c_printf(&b, "(%s)", expr);
                free(expr);
                if (b.oom) {
                    free(b.data);
                    if (err) {
                        err->code = CXPR_ERR_OUT_OF_MEMORY;
                        err->message = "Out of memory";
                    }
                    return NULL;
                }
                return b.data;
            }
            fn_name = cxpr_model_c_scoped_function_name(target_data->function_prefix,
                                                        entry->name);
            if (!fn_name) {
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            cxpr_model_c_printf(&b, "%s(", fn_name);
            free(fn_name);
            for (size_t i = 0u; i < argc; ++i) {
                char* arg;
                if (i > 0u) cxpr_model_c_puts(&b, ", ");
                arg = cxpr_expr_ast_to_c_at_offset(cxpr_expr_ast_call_arg(ast, i),
                                              lookback_offset, &target, err);
                if (!arg) {
                    free(b.data);
                    return NULL;
                }
                cxpr_model_c_puts(&b, arg);
                free(arg);
            }
            cxpr_model_c_puts(&b, ")");
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            return b.data;
        }
    }

    if (handled) *handled = false;
    return NULL;
}

char* cxpr_model_ast_expr_to_c(const cxpr_model_compiled* program,
                                      const cxpr_expr_ast* ast,
                                      const char* function_prefix,
                                      const double* literal_param_values,
                                      size_t literal_param_count,
                                      char** child_call_keys,
                                      size_t* child_call_child_indices,
                                      size_t child_call_count,
                                      cxpr_error* err) {
    cxpr_model_ast_c_target userdata = {
        .program = program,
        .function_prefix = function_prefix,
        .literal_param_values = literal_param_values,
        .literal_param_count = literal_param_count,
        .child_call_keys = child_call_keys,
        .child_call_child_indices = child_call_child_indices,
        .child_call_count = child_call_count,
    };
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = &userdata,
    };
    return cxpr_expr_ast_to_c(ast, &target, err);
}

static char* cxpr_model_ast_temp_make(cxpr_model_ast_temp_emit* emit,
                                      const char* expr,
                                      cxpr_error* err) {
    char name[64];
    if (!emit || !emit->declarations || !expr) return NULL;
    snprintf(name, sizeof(name), "_cx_t%zu", emit->next_temp++);
    cxpr_model_c_printf(emit->declarations, "    const double %s = %s;\n", name, expr);
    if (emit->declarations->oom) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return cxpr_strdup(name);
}

static char* cxpr_model_ast_binary_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                   const cxpr_expr_ast* ast,
                                                   cxpr_error* err) {
    int op = cxpr_expr_ast_operator(ast);
    const char* ops = NULL;
    char* left;
    char* right;
    cxpr_model_c_buf b = {0};

    switch (op) {
    case CXPR_TOK_PLUS: ops = "+"; break;
    case CXPR_TOK_MINUS: ops = "-"; break;
    case CXPR_TOK_STAR: ops = "*"; break;
    case CXPR_TOK_SLASH: ops = "/"; break;
    case CXPR_TOK_EQ: ops = "=="; break;
    case CXPR_TOK_NEQ: ops = "!="; break;
    case CXPR_TOK_LT: ops = "<"; break;
    case CXPR_TOK_GT: ops = ">"; break;
    case CXPR_TOK_LTE: ops = "<="; break;
    case CXPR_TOK_GTE: ops = ">="; break;
    case CXPR_TOK_AND: ops = "&&"; break;
    case CXPR_TOK_OR: ops = "||"; break;
    default:
        return cxpr_expr_ast_to_c(ast, emit ? emit->target : NULL, err);
    }

    left = cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_expr_ast_binary_left(ast), err);
    right = left ? cxpr_model_ast_expr_to_c_with_temps(emit, cxpr_expr_ast_binary_right(ast), err) : NULL;
    if (!left || !right) {
        free(left);
        free(right);
        return NULL;
    }
    cxpr_model_c_printf(&b, "(%s %s %s)", left, ops, right);
    free(left);
    free(right);
    if (b.oom) {
        free(b.data);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return NULL;
    }
    return b.data;
}

static char* cxpr_model_ast_expr_to_c_with_temps(cxpr_model_ast_temp_emit* emit,
                                                 const cxpr_expr_ast* ast,
                                                 cxpr_error* err) {
    if (!emit || !ast) return NULL;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_expr_ast_call_name(ast);
        const size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if ((cxpr_model_names_match(name, "min") || cxpr_model_names_match(name, "max")) &&
            argc == 2u) {
            const char* op = cxpr_model_names_match(name, "min") ? "<" : ">";
            char* left = cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_expr_ast_call_arg(ast, 0u), err);
            char* right = left ? cxpr_model_ast_expr_to_c_with_temps(
                emit, cxpr_expr_ast_call_arg(ast, 1u), err) : NULL;
            char* left_temp;
            char* right_temp;
            char* out;
            cxpr_model_c_buf b = {0};
            if (!left || !right) {
                free(left);
                free(right);
                return NULL;
            }
            left_temp = cxpr_model_ast_temp_make(emit, left, err);
            right_temp = left_temp ? cxpr_model_ast_temp_make(emit, right, err) : NULL;
            free(left);
            free(right);
            if (!left_temp || !right_temp) {
                free(left_temp);
                free(right_temp);
                return NULL;
            }
            cxpr_model_c_printf(&b, "((%s %s %s) ? %s : %s)",
                                left_temp, op, right_temp, left_temp, right_temp);
            free(left_temp);
            free(right_temp);
            if (b.oom) {
                free(b.data);
                if (err) {
                    err->code = CXPR_ERR_OUT_OF_MEMORY;
                    err->message = "Out of memory";
                }
                return NULL;
            }
            out = cxpr_model_ast_temp_make(emit, b.data, err);
            free(b.data);
            return out;
        }
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP) {
        return cxpr_model_ast_binary_to_c_with_temps(emit, ast, err);
    }
    return cxpr_expr_ast_to_c(ast, emit->target, err);
}

static bool cxpr_model_c_defined_function_used(const cxpr_model_compiled* program,
                                               const char* name,
                                               bool* used,
                                               cxpr_error* err) {
    if (!program || !program->registry || !name || !used) return true;
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        if (cxpr_model_names_match(entry->name, name) &&
            entry->defined_body &&
            entry->defined_return_field_count == 0u) {
            if (used[i]) return true;
            used[i] = true;
            return cxpr_model_c_collect_defined_function_refs(
                program, entry->defined_body, used, err);
        }
    }
    return true;
}

static bool cxpr_model_c_collect_defined_function_refs(const cxpr_model_compiled* program,
                                                       const cxpr_expr_ast* ast,
                                                       bool* used,
                                                       cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_binary_left(ast),
                                                          used, err) &&
               cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_binary_right(ast),
                                                          used, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_c_collect_defined_function_refs(program, cxpr_expr_ast_unary_operand(ast),
                                                          used, err);
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_expr_ast_call_name(ast);
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if (!cxpr_model_c_defined_function_used(program, name, used, err)) return false;
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_expr_ast_call_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        {
            cxpr_func_entry* entry = program && program->registry
                ? cxpr_registry_find(program->registry, cxpr_expr_ast_producer_name(ast))
                : NULL;
            if (entry && !entry->model_producer && entry->defined_return_field_count > 0u) {
                const char* field = cxpr_expr_ast_producer_field(ast);
                for (size_t f = 0u; f < entry->defined_return_field_count; ++f) {
                    if (entry->defined_return_field_names[f] &&
                        field &&
                        strcmp(entry->defined_return_field_names[f], field) == 0) {
                        if (!cxpr_model_c_collect_defined_function_refs(
                                program, entry->defined_return_field_bodies[f], used, err)) {
                            return false;
                        }
                        break;
                    }
                }
            }
        }
        for (size_t i = 0u; i < cxpr_expr_ast_producer_arg_count(ast); ++i) {
            if (!cxpr_model_c_collect_defined_function_refs(
                    program, cxpr_expr_ast_producer_arg(ast, i), used, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_INDEX:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_index_target(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_index_expression(ast), used, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_condition(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_true(ast), used, err) &&
               cxpr_model_c_collect_defined_function_refs(
                   program, cxpr_expr_ast_ternary_false(ast), used, err);
    default:
        return true;
    }
}

static char* cxpr_model_ast_defined_fn_to_c(const cxpr_model_compiled* program,
                                            const cxpr_func_entry* entry,
                                            const char* function_prefix,
                                            cxpr_error* err) {
    cxpr_model_c_buf b = {0};
    char* fn_name;
    cxpr_model_ast_c_target userdata = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = &userdata,
    };
    if (!entry || !entry->defined_body) return NULL;
    userdata.program = program;
    userdata.param_names = entry->defined_param_names;
    userdata.param_exprs = NULL;
    userdata.param_count = entry->defined_param_count;
    userdata.inline_fn_name = entry->name;
    userdata.function_prefix = function_prefix;
    fn_name = cxpr_model_c_scoped_function_name(function_prefix, entry->name);
    if (!fn_name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    if (function_prefix && function_prefix[0]) {
        cxpr_model_c_printf(&b, "/* Source function: %s (scope: %s) */\n",
                            entry->name ? entry->name : "(unnamed)",
                            function_prefix);
    } else {
        cxpr_model_c_printf(&b, "/* Source function: %s */\n",
                            entry->name ? entry->name : "(unnamed)");
    }
    cxpr_model_c_printf(&b, "static inline double %s(", fn_name);
    free(fn_name);
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        char* param_name = cxpr_model_c_safe_name(entry->defined_param_names[i]);
        if (!param_name) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        if (i > 0u) cxpr_model_c_puts(&b, ", ");
        cxpr_model_c_printf(&b, "double %s", param_name);
        free(param_name);
    }
    if (entry->defined_param_count == 0u) cxpr_model_c_puts(&b, "void");
    cxpr_model_c_puts(&b, ") { return ");
    {
        cxpr_model_c_buf declarations = {0};
        cxpr_model_ast_temp_emit temp_emit = {
            .declarations = &declarations,
            .target = &target,
            .next_temp = 0u,
        };
        char* expr = cxpr_model_ast_expr_to_c_with_temps(&temp_emit,
                                                         entry->defined_body,
                                                         err);
        if (!expr) {
            free(declarations.data);
            free(b.data);
            return NULL;
        }
        if (declarations.data && declarations.len > 0u) {
            size_t prefix_len = b.len;
            cxpr_model_c_buf nb = {0};
            if (b.len >= strlen(" { return ") &&
                strcmp(b.data + b.len - strlen(" { return "), " { return ") == 0) {
                prefix_len = b.len - strlen("return ");
            }
            cxpr_model_c_reserve(&nb, prefix_len + declarations.len + strlen("    return ") + strlen(expr) + 16u);
            if (!nb.oom) {
                memcpy(nb.data, b.data, prefix_len);
                nb.len = prefix_len;
                nb.data[nb.len] = '\0';
                cxpr_model_c_puts(&nb, "\n");
                cxpr_model_c_puts(&nb, declarations.data);
                cxpr_model_c_puts(&nb, "    return ");
                free(b.data);
                b = nb;
            }
        }
        free(declarations.data);
        cxpr_model_c_puts(&b, expr);
        free(expr);
    }
    cxpr_model_c_puts(&b, "; }\n\n");
    if (b.oom) {
        free(b.data);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    return b.data;
}

bool cxpr_model_c_emit_defined_functions_ast(const cxpr_model_compiled* program,
                                                    const char* function_prefix,
                                                    cxpr_model_c_buf* b,
                                                    cxpr_error* err) {
    bool* used = NULL;
    if (!program || !program->registry) return true;
    used = (bool*)calloc(program->registry->count ? program->registry->count : 1u,
                         sizeof(bool));
    if (!used && program->registry->count > 0u) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (!cxpr_model_c_collect_defined_function_refs(
                program, program->bindings[i].ast, used, err)) {
            free(used);
            return false;
        }
    }
    for (size_t i = 0u; i < program->registry->count; ++i) {
        cxpr_func_entry* entry = &program->registry->entries[i];
        char* source;
        if (!used[i]) continue;
        if (!entry->defined_body || entry->defined_return_field_count > 0u) continue;
        source = cxpr_model_ast_defined_fn_to_c(program, entry, function_prefix, err);
        if (!source) {
            free(used);
            return false;
        }
        cxpr_model_c_puts(b, source);
        free(source);
        if (b->oom) {
            free(used);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    free(used);
    return true;
}

static bool cxpr_model_c_child_is_used(const size_t* child_call_child_indices,
                                       size_t child_call_count,
                                       size_t child_index) {
    for (size_t i = 0u; i < child_call_count; ++i) {
        if (child_call_child_indices[i] == child_index) return true;
    }
    return false;
}

static const char* cxpr_model_c_common_helpers_source(void) {
    return "#include <cxpr/model/runtime.h>\n\n"
           "#include <stdint.h>\n\n";
}

bool cxpr_model_c_emit_child_model_helpers(
    const cxpr_model_compiled* program,
    const char* function_prefix,
    const size_t* child_call_child_indices,
    size_t child_call_count,
    cxpr_model_c_buf* b,
    cxpr_error* err) {
    if (!program || !function_prefix || !b) return true;
    for (size_t i = 0u; i < program->child_count; ++i) {
        const cxpr_model_compiled* child = program->children[i].program;
        char* tick_name;
        char* tick_source;
        const char* nested_source;
        if (!cxpr_model_c_child_is_used(
                child_call_child_indices, child_call_count, i)) {
            continue;
        }
        if (!child) continue;
        tick_name = cxpr_model_c_child_tick_name(function_prefix, i);
        if (!tick_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
        cxpr_model_c_printf(b, "/* Source model tick: %s */\n",
                            program->children[i].name ? program->children[i].name : "(unnamed)");
        tick_source = cxpr_model_compiled_generate_c_outputs(
            child, "static inline", tick_name, NULL, 0u, err);
        if (!tick_source) {
            free(tick_name);
            return false;
        }
        nested_source = tick_source;
        if (strncmp(nested_source,
                    cxpr_model_c_common_helpers_source(),
                    strlen(cxpr_model_c_common_helpers_source())) == 0) {
            nested_source += strlen(cxpr_model_c_common_helpers_source());
        }
        cxpr_model_c_puts(b, nested_source);
        cxpr_model_c_puts(b, "\n");
        free(tick_source);

        for (size_t field_i = 0u; field_i < child->output_count; ++field_i) {
            char* helper_name;
            helper_name = cxpr_model_c_child_field_name(function_prefix, i, field_i);
            if (!helper_name) {
                free(tick_name);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
            cxpr_model_c_printf(b, "/* Source model field: %s.%s */\n",
                                program->children[i].name ? program->children[i].name : "(unnamed)",
                                child->outputs[field_i] ? child->outputs[field_i] : "(unnamed)");
            cxpr_model_c_printf(b,
                                "static inline double %s(uint8_t* restrict _cx_child_initialized, double* restrict _cx_child_outputs, %s_state* restrict _cx_child_state",
                                helper_name,
                                tick_name);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (!input_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", input_name);
                free(input_name);
            }
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (!param_name) {
                    free(helper_name);
                    free(tick_name);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                    return false;
                }
                cxpr_model_c_printf(b, ", double %s", param_name);
                free(param_name);
            }
            cxpr_model_c_puts(b, ") {\n");
            cxpr_model_c_printf(b, "    double _cx_child_inputs[%zu] = {",
                                child->input_count ? child->input_count : 1u);
            for (size_t in_i = 0u; in_i < child->input_count; ++in_i) {
                char* input_name = cxpr_model_c_safe_name(child->inputs[in_i]);
                if (in_i > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, input_name ? input_name : "0.0");
                free(input_name);
            }
            if (child->input_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_printf(b, "    double _cx_child_params[%zu] = {",
                                child->constant_count ? child->constant_count : 1u);
            for (size_t p = 0u; p < child->constant_count; ++p) {
                char* param_name = cxpr_model_c_prefixed_name("param_", child->constants[p].name);
                if (p > 0u) cxpr_model_c_puts(b, ", ");
                cxpr_model_c_puts(b, param_name ? param_name : "0.0");
                free(param_name);
            }
            if (child->constant_count == 0u) cxpr_model_c_puts(b, "0.0");
            cxpr_model_c_puts(b, "};\n");
            cxpr_model_c_puts(b, "    if (*_cx_child_initialized == 0u) {\n");
            cxpr_model_c_printf(b,
                                "        %s(_cx_child_state, _cx_child_inputs, _cx_child_params, _cx_child_outputs);\n",
                                tick_name);
            cxpr_model_c_puts(b, "        *_cx_child_initialized = 1u;\n");
            cxpr_model_c_puts(b, "    }\n");
            cxpr_model_c_printf(b, "    return _cx_child_outputs[%zu];\n", field_i);
            cxpr_model_c_puts(b, "}\n\n");
            free(helper_name);
        }
        free(tick_name);
        if (b->oom) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    return true;
}

void cxpr_model_c_emit_common_helpers(cxpr_model_c_buf* b) {
    cxpr_model_c_puts(b, cxpr_model_c_common_helpers_source());
}
