#include "model/window/plan.h"

#include "eval/internal.h"
#include "model/window/window.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static size_t cxpr_model_window_plan_param_index(const cxpr_model_compiled* program,
                                                 const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) return i;
    }
    return (size_t)-1;
}

static bool cxpr_model_window_plan_constant_expr(const cxpr_model_compiled* program,
                                                 const cxpr_expr_ast* ast,
                                                 double* out) {
    double left = 0.0;
    double right = 0.0;
    int op;
    if (!ast || !out) return false;
    if (cxpr_eval_constant_double(ast, out)) return true;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_VARIABLE) {
        const char* name = cxpr_expr_ast_param_name(ast);
        size_t index = cxpr_model_window_plan_param_index(program, name);
        return index != (size_t)-1 &&
               program->constants[index].ast &&
               cxpr_eval_constant_double(program->constants[index].ast, out);
    }
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL) {
        const char* name = cxpr_expr_ast_call_name(ast);
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        if ((!cxpr_model_names_match(name, "min") &&
             !cxpr_model_names_match(name, "max")) || argc == 0u ||
            !cxpr_model_window_plan_constant_expr(
                program, cxpr_expr_ast_call_arg(ast, 0u), out)) {
            return false;
        }
        for (size_t i = 1u; i < argc; ++i) {
            double value = 0.0;
            if (!cxpr_model_window_plan_constant_expr(
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
    if (!cxpr_model_window_plan_constant_expr(program, cxpr_expr_ast_binary_left(ast), &left) ||
        !cxpr_model_window_plan_constant_expr(program, cxpr_expr_ast_binary_right(ast), &right)) {
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

static bool cxpr_model_window_plan_period_capacity(const cxpr_model_compiled* program,
                                                   const cxpr_expr_ast* period_ast,
                                                   size_t* out_capacity,
                                                   cxpr_error* err) {
    double raw = 0.0;
    long period;
    (void)err;
    if (!period_ast || !out_capacity) return false;
    if (cxpr_expr_ast_kind_of(period_ast) == CXPR_NODE_VARIABLE) {
        size_t index = cxpr_model_window_plan_param_index(
            program, cxpr_expr_ast_param_name(period_ast));
        if (index != (size_t)-1 &&
            program->constants[index].has_max_value &&
            isfinite(program->constants[index].max_value)) {
            raw = program->constants[index].max_value;
            goto resolved;
        }
    }
    if (!cxpr_model_window_plan_constant_expr(program, period_ast, &raw)) raw = 512.0;
resolved:
    if (!isfinite(raw) || raw < 1.0) raw = 1.0;
    period = lround(raw);
    if (period < 1) period = 1;
    *out_capacity = (size_t)period;
    return true;
}

static cxpr_model_window_plan_op cxpr_model_window_plan_op_for_name(const char* name) {
    const cxpr_window_ir* window = cxpr_window_ir_find(name);
    return window ? window->op : CXPR_MODEL_WINDOW_PLAN_OP_NONE;
}

static bool cxpr_model_window_plan_append(cxpr_model_window_plan* plan,
                                          const cxpr_model_window_plan_node* node,
                                          size_t* out_index,
                                          cxpr_error* err) {
    cxpr_model_window_plan_node* grown;
    if (!plan || !node) return false;
    for (size_t i = 0u; i < plan->node_count; ++i) {
        const cxpr_model_window_plan_node* existing = &plan->nodes[i];
        if (existing->op == node->op &&
            existing->period_capacity == node->period_capacity &&
            existing->has_child == node->has_child &&
            (!node->has_child || existing->child_index == node->child_index) &&
            cxpr_model_ast_equal(existing->ast, node->ast)) {
            if (out_index) *out_index = i;
            return true;
        }
    }
    grown = (cxpr_model_window_plan_node*)realloc(
        plan->nodes, (plan->node_count + 1u) * sizeof(cxpr_model_window_plan_node));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    plan->nodes = grown;
    plan->nodes[plan->node_count] = *node;
    if (out_index) *out_index = plan->node_count;
    plan->node_count++;
    return true;
}

static bool cxpr_model_window_plan_add_nested_roc_aggregate(
    const cxpr_model_compiled* program,
    cxpr_model_window_plan* plan,
    const cxpr_expr_ast* ast,
    cxpr_model_window_plan_op op,
    cxpr_error* err) {
    const cxpr_expr_ast* roc_ast;
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* roc_period_ast;
    const cxpr_expr_ast* aggregate_period_ast;
    size_t roc_capacity = 0u;
    size_t aggregate_capacity = 0u;
    size_t roc_index = 0u;
    cxpr_model_window_plan_node roc_node = {0};
    cxpr_model_window_plan_node aggregate_node = {0};

    if (!program || !plan || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(ast) != 2u) {
        return true;
    }
    if (op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
        op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) {
        return true;
    }
    roc_ast = cxpr_expr_ast_call_arg(ast, 0u);
    if (!roc_ast || cxpr_expr_ast_kind_of(roc_ast) != CXPR_NODE_FUNCTION_CALL ||
        !cxpr_model_names_match(cxpr_expr_ast_call_name(roc_ast), "__cxpr_window_roc") ||
        cxpr_expr_ast_call_arg_count(roc_ast) != 2u) {
        return true;
    }
    value_ast = cxpr_expr_ast_call_arg(roc_ast, 0u);
    roc_period_ast = cxpr_expr_ast_call_arg(roc_ast, 1u);
    aggregate_period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    if (!cxpr_model_window_plan_period_capacity(
            program, roc_period_ast, &roc_capacity, err) ||
        !cxpr_model_window_plan_period_capacity(
            program, aggregate_period_ast, &aggregate_capacity, err)) {
        return false;
    }

    roc_node.ast = roc_ast;
    roc_node.op = CXPR_MODEL_WINDOW_PLAN_OP_ROC;
    roc_node.value_ast = value_ast;
    roc_node.period_ast = roc_period_ast;
    roc_node.period_capacity = roc_capacity;
    if (!cxpr_model_window_plan_append(plan, &roc_node, &roc_index, err)) return false;

    aggregate_node.ast = ast;
    aggregate_node.op = op;
    aggregate_node.value_ast = value_ast;
    aggregate_node.period_ast = aggregate_period_ast;
    aggregate_node.period_capacity = aggregate_capacity;
    aggregate_node.slot_offset = plan->slot_count;
    aggregate_node.slot_count = 4u + aggregate_capacity;
    aggregate_node.child_index = roc_index;
    aggregate_node.has_child = true;
    {
        size_t before = plan->node_count;
        size_t aggregate_index = 0u;
        if (!cxpr_model_window_plan_append(
                plan, &aggregate_node, &aggregate_index, err)) {
            return false;
        }
        if (aggregate_index == before) plan->slot_count += aggregate_node.slot_count;
    }
    return true;
}

static bool cxpr_model_window_plan_add_simple_aggregate(
    const cxpr_model_compiled* program,
    cxpr_model_window_plan* plan,
    const cxpr_expr_ast* ast,
    cxpr_model_window_plan_op op,
    cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* period_ast;
    size_t capacity = 0u;
    cxpr_model_window_plan_node node = {0};

    if (!program || !plan || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        cxpr_expr_ast_call_arg_count(ast) != 2u) {
        return true;
    }
    if (op != CXPR_MODEL_WINDOW_PLAN_OP_MEAN &&
        op != CXPR_MODEL_WINDOW_PLAN_OP_SUM) {
        return true;
    }
    value_ast = cxpr_expr_ast_call_arg(ast, 0u);
    if (cxpr_expr_ast_kind_of(value_ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_model_window_is_function(cxpr_expr_ast_call_name(value_ast))) {
        return true;
    }
    period_ast = cxpr_expr_ast_call_arg(ast, 1u);
    if (!cxpr_model_window_plan_period_capacity(program, period_ast, &capacity, err)) {
        return false;
    }

    node.ast = ast;
    node.op = op;
    node.value_ast = value_ast;
    node.period_ast = period_ast;
    node.period_capacity = capacity;
    node.slot_offset = plan->slot_count;
    node.slot_count = 4u + capacity;
    {
        size_t before = plan->node_count;
        size_t node_index = 0u;
        if (!cxpr_model_window_plan_append(plan, &node, &node_index, err)) {
            return false;
        }
        if (node_index == before) plan->slot_count += node.slot_count;
    }
    return true;
}

static bool cxpr_model_window_plan_visit(const cxpr_model_compiled* program,
                                         cxpr_model_window_plan* plan,
                                         const cxpr_expr_ast* ast,
                                         cxpr_error* err) {
    cxpr_model_window_plan_op op;
    if (!ast) return true;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_FUNCTION_CALL:
        op = cxpr_model_window_plan_op_for_name(cxpr_expr_ast_call_name(ast));
        if (op != CXPR_MODEL_WINDOW_PLAN_OP_NONE) {
            if (!cxpr_model_window_plan_add_nested_roc_aggregate(
                    program, plan, ast, op, err)) {
                return false;
            }
            if (!cxpr_model_window_plan_add_simple_aggregate(
                    program, plan, ast, op, err)) {
                return false;
            }
        }
        for (size_t i = 0u; i < cxpr_expr_ast_call_arg_count(ast); ++i) {
            if (!cxpr_model_window_plan_visit(
                    program, plan, cxpr_expr_ast_call_arg(ast, i), err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_binary_left(ast), err) &&
               cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_binary_right(ast), err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_unary_operand(ast), err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_ternary_condition(ast), err) &&
               cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_ternary_true(ast), err) &&
               cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_ternary_false(ast), err);
    case CXPR_NODE_INDEX:
        return cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_index_target(ast), err) &&
               cxpr_model_window_plan_visit(program, plan, cxpr_expr_ast_index_expression(ast), err);
    default:
        return true;
    }
}

bool cxpr_model_window_plan_build(const cxpr_model_compiled* program,
                                  cxpr_model_window_plan* out,
                                  cxpr_error* err) {
    if (!out) return false;
    *out = (cxpr_model_window_plan){0};
    if (!program || !program->has_fused_layout) return true;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        const cxpr_expr_ast* ast =
            program->bindings[i].ast;
        if (!cxpr_model_window_plan_visit(program, out, ast, err)) {
            cxpr_model_window_plan_free(out);
            return false;
        }
    }
    return true;
}

void cxpr_model_window_plan_free(cxpr_model_window_plan* plan) {
    if (!plan) return;
    free(plan->nodes);
    *plan = (cxpr_model_window_plan){0};
}

const cxpr_model_window_plan_node* cxpr_model_window_plan_find_ast(
    const cxpr_model_window_plan* plan,
    const cxpr_expr_ast* ast) {
    if (!plan || !ast) return NULL;
    for (size_t i = 0u; i < plan->node_count; ++i) {
        if (plan->nodes[i].ast == ast || cxpr_model_ast_equal(plan->nodes[i].ast, ast)) {
            return &plan->nodes[i];
        }
    }
    return NULL;
}

size_t cxpr_model_window_plan_slot_count(const cxpr_model_window_plan* plan) {
    return plan ? plan->slot_count : 0u;
}
