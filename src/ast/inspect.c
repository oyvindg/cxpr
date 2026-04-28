/**
 * @file inspect.c
 * @brief AST inspection helpers.
 */

#include "internal.h"

#include <string.h>

static bool cxpr_ast_named_args_present(char* const* arg_names, size_t argc) {
    if (!arg_names) return false;
    for (size_t i = 0; i < argc; ++i) {
        if (arg_names[i]) return true;
    }
    return false;
}

static bool cxpr_ast_branches_are_boolean(const cxpr_ast* left, const cxpr_ast* right) {
    return cxpr_ast_is_boolean_expression(left) && cxpr_ast_is_boolean_expression(right);
}

bool cxpr_ast_call_uses_named_args(const cxpr_ast* ast) {
    if (!ast) return false;
    if (ast->type == CXPR_NODE_FUNCTION_CALL) {
        return cxpr_ast_named_args_present(ast->data.function_call.arg_names,
                                           ast->data.function_call.argc);
    }
    if (ast->type == CXPR_NODE_PRODUCER_ACCESS) {
        return cxpr_ast_named_args_present(ast->data.producer_access.arg_names,
                                           ast->data.producer_access.argc);
    }
    return false;
}

const char* cxpr_ast_full_reference(const cxpr_ast* ast) {
    if (!ast) return NULL;
    if (ast->type == CXPR_NODE_FIELD_ACCESS) return ast->data.field_access.full_key;
    if (ast->type == CXPR_NODE_CHAIN_ACCESS) return ast->data.chain_access.full_key;
    return NULL;
}

cxpr_node_type cxpr_ast_type(const cxpr_ast* ast) {
    return ast ? ast->type : CXPR_NODE_NUMBER;
}

double cxpr_ast_number_value(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_NUMBER) ? ast->data.number.value : 0.0;
}

bool cxpr_ast_bool_value(const cxpr_ast* ast) {
    return ast && ast->type == CXPR_NODE_BOOL && ast->data.boolean.value;
}

const char* cxpr_ast_string_value(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_STRING) ? ast->data.string.value : NULL;
}

const char* cxpr_ast_identifier_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_IDENTIFIER) ? ast->data.identifier.name : NULL;
}

const char* cxpr_ast_variable_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_VARIABLE) ? ast->data.variable.name : NULL;
}

const char* cxpr_ast_field_object(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FIELD_ACCESS) ? ast->data.field_access.object : NULL;
}

const char* cxpr_ast_field_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FIELD_ACCESS) ? ast->data.field_access.field : NULL;
}

size_t cxpr_ast_chain_depth(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_CHAIN_ACCESS) ? ast->data.chain_access.depth : 0;
}

const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_CHAIN_ACCESS) return NULL;
    if (index >= ast->data.chain_access.depth) return NULL;
    return ast->data.chain_access.path[index];
}

int cxpr_ast_operator(const cxpr_ast* ast) {
    if (!ast) return 0;
    if (ast->type == CXPR_NODE_BINARY_OP) return ast->data.binary_op.op;
    if (ast->type == CXPR_NODE_UNARY_OP) return ast->data.unary_op.op;
    return 0;
}

const cxpr_ast* cxpr_ast_left(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_BINARY_OP) ? ast->data.binary_op.left : NULL;
}

const cxpr_ast* cxpr_ast_right(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_BINARY_OP) ? ast->data.binary_op.right : NULL;
}

const cxpr_ast* cxpr_ast_operand(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_UNARY_OP) ? ast->data.unary_op.operand : NULL;
}

const char* cxpr_ast_function_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FUNCTION_CALL) ? ast->data.function_call.name : NULL;
}

size_t cxpr_ast_function_argc(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FUNCTION_CALL) ? ast->data.function_call.argc : 0;
}

const cxpr_ast* cxpr_ast_function_arg(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL) return NULL;
    if (index >= ast->data.function_call.argc) return NULL;
    return ast->data.function_call.args[index];
}

const char* cxpr_ast_function_arg_name(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL) return NULL;
    if (!ast->data.function_call.arg_names || index >= ast->data.function_call.argc) return NULL;
    return ast->data.function_call.arg_names[index];
}

bool cxpr_ast_function_has_named_args(const cxpr_ast* ast) {
    return ast && ast->type == CXPR_NODE_FUNCTION_CALL && cxpr_ast_call_uses_named_args(ast);
}

const cxpr_ast* cxpr_ast_lookback_target(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_LOOKBACK) ? ast->data.lookback.target : NULL;
}

const cxpr_ast* cxpr_ast_lookback_index(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_LOOKBACK) ? ast->data.lookback.index : NULL;
}

const char* cxpr_ast_producer_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.name : NULL;
}

const char* cxpr_ast_producer_field(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.field : NULL;
}

size_t cxpr_ast_producer_argc(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.argc : 0;
}

const cxpr_ast* cxpr_ast_producer_arg(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (index >= ast->data.producer_access.argc) return NULL;
    return ast->data.producer_access.args[index];
}

const char* cxpr_ast_producer_arg_name(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (!ast->data.producer_access.arg_names || index >= ast->data.producer_access.argc) return NULL;
    return ast->data.producer_access.arg_names[index];
}

bool cxpr_ast_producer_has_named_args(const cxpr_ast* ast) {
    return ast && ast->type == CXPR_NODE_PRODUCER_ACCESS && cxpr_ast_call_uses_named_args(ast);
}

const cxpr_ast* cxpr_ast_ternary_condition(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.condition : NULL;
}

const cxpr_ast* cxpr_ast_ternary_true_branch(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.true_branch : NULL;
}

const cxpr_ast* cxpr_ast_ternary_false_branch(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.false_branch : NULL;
}

bool cxpr_ast_is_boolean_expression(const cxpr_ast* ast) {
    const char* name;

    if (!ast) return false;

    switch (ast->type) {
        case CXPR_NODE_BOOL:
            return true;
        case CXPR_NODE_BINARY_OP:
            switch (ast->data.binary_op.op) {
                case CXPR_TOK_EQ:
                case CXPR_TOK_NEQ:
                case CXPR_TOK_LT:
                case CXPR_TOK_GT:
                case CXPR_TOK_LTE:
                case CXPR_TOK_GTE:
                case CXPR_TOK_AND:
                case CXPR_TOK_OR:
                case CXPR_TOK_IN:
                    return true;
                default:
                    return false;
            }
        case CXPR_NODE_UNARY_OP:
            return ast->data.unary_op.op == CXPR_TOK_NOT;
        case CXPR_NODE_FUNCTION_CALL:
            name = ast->data.function_call.name;
            return name != NULL &&
                   (strcmp(name, "cross_above") == 0 ||
                    strcmp(name, "cross_below") == 0);
        case CXPR_NODE_TERNARY:
            return cxpr_ast_is_boolean_expression(ast->data.ternary.condition) &&
                   cxpr_ast_branches_are_boolean(ast->data.ternary.true_branch,
                                                ast->data.ternary.false_branch);
        default:
            return false;
    }
}
