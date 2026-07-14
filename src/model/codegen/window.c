#include "model/codegen/internal.h"

static const char* cxpr_model_c_window_aggregate_result_expr(
    cxpr_model_window_plan_op op,
    const char* sum_expr) {
    return op == CXPR_MODEL_WINDOW_PLAN_OP_MEAN
               ? "_cx_sum / (double)_cx_count"
               : sum_expr;
}

bool cxpr_model_c_emit_planned_roc_rolling_update(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_model_window_plan_node* node,
    size_t node_index,
    size_t roc_capacity,
    size_t aggregate_capacity,
    const char* counter_type,
    const char* now_expr,
    const char* prev_expr,
    cxpr_error* err) {
    if (!b || !name || !node || !counter_type || !now_expr || !prev_expr) return false;
    cxpr_model_c_puts(
        b,
        "        /* Keep default-period rolling state warm even when runtime params use fallback. */\n");
    cxpr_model_c_printf(
        b,
        "        { size_t _cx_next = (size_t)_cx_state->window_%zu.next; size_t _cx_count = (size_t)_cx_state->window_%zu.count; double _cx_sum = _cx_state->window_%zu.sum; double _cx_now = %s; double _cx_prev = %s; double _cx_roc = isnan(_cx_now) ? NAN : ((isnan(_cx_prev) || fabs(_cx_prev) <= 1e-12) ? 0.0 : ((_cx_now - _cx_prev) / _cx_prev) * 100.0); double _cx_old = _cx_state->window_%zu.values[_cx_next]; if (!isnan(_cx_old)) { _cx_sum -= _cx_old; if (_cx_count > 0u) _cx_count--; } if (!isnan(_cx_roc)) { _cx_sum += _cx_roc; _cx_count++; } _cx_state->window_%zu.values[_cx_next] = _cx_roc; _cx_state->window_%zu.next = (%s)((_cx_next + 1u) %% %zuu); _cx_state->window_%zu.count = (%s)_cx_count; _cx_state->window_%zu.sum = _cx_sum; if (!CXPR_UNLIKELY(_cx_rp != %zuu || _cx_ap != %zuu)) { %s = _cx_count == 0u ? 0.0 : %s; } else {\n",
        node_index,
        node_index,
        node_index,
        now_expr,
        prev_expr,
        node_index,
        node_index,
        node_index,
        counter_type,
        aggregate_capacity,
        node_index,
        counter_type,
        node_index,
        roc_capacity,
        aggregate_capacity,
        name,
        cxpr_model_c_window_aggregate_result_expr(node->op, "_cx_sum"));
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}

bool cxpr_model_c_emit_planned_roc_aggregate_fallback(
    cxpr_model_c_buf* b,
    const char* name,
    const cxpr_ast* value_ast,
    cxpr_model_window_plan_op op,
    const cxpr_c_target* target,
    const cxpr_model_program* program,
    cxpr_error* err) {
    if (!b || !name || !value_ast || !target || !program) return false;
    cxpr_model_c_puts(
        b,
        "            { double _cx_sum = 0.0; size_t _cx_count = 0u;\n"
        "        for (size_t _cx_i = 0u; _cx_i < _cx_ap; ++_cx_i) {\n");
    if (!cxpr_model_c_emit_dynamic_history_value(
            b, "_cx_now", value_ast, "_cx_i", target, program, err) ||
        !cxpr_model_c_emit_dynamic_history_value(
            b, "_cx_prev", value_ast, "(_cx_i + _cx_rp)", target, program, err)) {
        return false;
    }
    cxpr_model_c_puts(
        b,
        "            if (!isnan(_cx_now)) { _cx_sum += (isnan(_cx_prev) || fabs(_cx_prev) <= 1e-12) ? 0.0 : ((_cx_now - _cx_prev) / _cx_prev) * 100.0; _cx_count++; }\n"
        "        }\n");
    cxpr_model_c_printf(
        b,
        "        %s = _cx_count == 0u ? 0.0 : %s; }\n",
        name,
        cxpr_model_c_window_aggregate_result_expr(op, "_cx_sum"));
    if (b->oom) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    return true;
}
