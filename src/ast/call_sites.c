/**
 * @file call_sites.c
 * @brief Provider-independent call-site metadata traversal.
 */

#include "internal.h"

#include <cxpr/analysis.h>

static bool cxpr_visit_static_named_string_args_node(
    const cxpr_ast* ast,
    cxpr_static_named_string_arg_visitor visitor,
    void* userdata);

static bool cxpr_visit_call_args(
    cxpr_call_site_kind kind,
    const cxpr_ast* call,
    const char* callee,
    cxpr_ast* const* args,
    char* const* arg_names,
    size_t argc,
    cxpr_static_named_string_arg_visitor visitor,
    void* userdata) {
    size_t i;
    for (i = 0u; i < argc; ++i) {
        const char* value = cxpr_ast_string_value(args[i]);
        if (arg_names && arg_names[i] && value) {
            const cxpr_static_named_string_arg found = {
                kind,
                call,
                callee,
                arg_names[i],
                value,
            };
            if (!visitor(&found, userdata)) return false;
        }
        if (!cxpr_visit_static_named_string_args_node(
                args[i], visitor, userdata)) {
            return false;
        }
    }
    return true;
}

static bool cxpr_visit_static_named_string_args_node(
    const cxpr_ast* ast,
    cxpr_static_named_string_arg_visitor visitor,
    void* userdata) {
    size_t i;
    if (!ast) return true;
    switch (ast->type) {
    case CXPR_NODE_ARRAY:
        for (i = 0u; i < ast->data.array.count; ++i) {
            if (!cxpr_visit_static_named_string_args_node(
                    ast->data.array.elements[i], visitor, userdata)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_RECORD:
        for (i = 0u; i < ast->data.record.field_count; ++i) {
            if (!cxpr_visit_static_named_string_args_node(
                    ast->data.record.field_values[i], visitor, userdata)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_visit_static_named_string_args_node(
            ast->data.field_access.base, visitor, userdata);
    case CXPR_NODE_PRODUCER_ACCESS:
        return cxpr_visit_call_args(
            CXPR_CALL_SITE_PRODUCER,
            ast,
            ast->data.producer_access.name,
            ast->data.producer_access.args,
            ast->data.producer_access.arg_names,
            ast->data.producer_access.argc,
            visitor,
            userdata);
    case CXPR_NODE_BINARY_OP:
        return cxpr_visit_static_named_string_args_node(
                   ast->data.binary_op.left, visitor, userdata) &&
               cxpr_visit_static_named_string_args_node(
                   ast->data.binary_op.right, visitor, userdata);
    case CXPR_NODE_UNARY_OP:
        return cxpr_visit_static_named_string_args_node(
            ast->data.unary_op.operand, visitor, userdata);
    case CXPR_NODE_FUNCTION_CALL:
        return cxpr_visit_call_args(
            CXPR_CALL_SITE_FUNCTION,
            ast,
            ast->data.function_call.name,
            ast->data.function_call.args,
            ast->data.function_call.arg_names,
            ast->data.function_call.argc,
            visitor,
            userdata);
    case CXPR_NODE_LOOKBACK:
        return cxpr_visit_static_named_string_args_node(
                   ast->data.lookback.target, visitor, userdata) &&
               cxpr_visit_static_named_string_args_node(
                   ast->data.lookback.index, visitor, userdata);
    case CXPR_NODE_TERNARY:
        return cxpr_visit_static_named_string_args_node(
                   ast->data.ternary.condition, visitor, userdata) &&
               cxpr_visit_static_named_string_args_node(
                   ast->data.ternary.true_branch, visitor, userdata) &&
               cxpr_visit_static_named_string_args_node(
                   ast->data.ternary.false_branch, visitor, userdata);
    default:
        return true;
    }
}

bool cxpr_visit_static_named_string_args(
    const cxpr_ast* ast,
    cxpr_static_named_string_arg_visitor visitor,
    void* userdata) {
    if (!ast || !visitor) return false;
    return cxpr_visit_static_named_string_args_node(ast, visitor, userdata);
}
