/**
 * @file free.c
 * @brief AST destruction helpers.
 */

#include "internal.h"
#include <stdlib.h>

static void cxpr_ast_free_arg_names(char** arg_names, size_t argc) {
    if (!arg_names) return;
    for (size_t i = 0; i < argc; ++i) free(arg_names[i]);
    free(arg_names);
}

void cxpr_ast_free(cxpr_ast* ast) {
    if (!ast) return;

    switch (ast->type) {
        case CXPR_NODE_NUMBER:
        case CXPR_NODE_BOOL:
            break;
        case CXPR_NODE_STRING:
            free(ast->data.string.value);
            break;
        case CXPR_NODE_IDENTIFIER:
            free(ast->data.identifier.name);
            break;
        case CXPR_NODE_VARIABLE:
            free(ast->data.variable.name);
            break;
        case CXPR_NODE_FIELD_ACCESS:
            free(ast->data.field_access.object);
            free(ast->data.field_access.field);
            free(ast->data.field_access.full_key);
            break;
        case CXPR_NODE_CHAIN_ACCESS:
            if (ast->data.chain_access.path) {
                for (size_t i = 0; i < ast->data.chain_access.depth; ++i) {
                    free(ast->data.chain_access.path[i]);
                }
                free(ast->data.chain_access.path);
            }
            free(ast->data.chain_access.full_key);
            break;
        case CXPR_NODE_PRODUCER_ACCESS:
            free(ast->data.producer_access.name);
            free(ast->data.producer_access.field);
            free(ast->data.producer_access.full_key);
            free(ast->data.producer_access.cached_const_key);
            if (ast->data.producer_access.args) {
                for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                    cxpr_ast_free(ast->data.producer_access.args[i]);
                }
                free(ast->data.producer_access.args);
            }
            cxpr_ast_free_arg_names(ast->data.producer_access.arg_names,
                                    ast->data.producer_access.argc);
            break;
        case CXPR_NODE_BINARY_OP:
            cxpr_ast_free(ast->data.binary_op.left);
            cxpr_ast_free(ast->data.binary_op.right);
            break;
        case CXPR_NODE_UNARY_OP:
            cxpr_ast_free(ast->data.unary_op.operand);
            break;
        case CXPR_NODE_FUNCTION_CALL:
            free(ast->data.function_call.name);
            free(ast->data.function_call.cached_const_key);
            if (ast->data.function_call.args) {
                for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                    cxpr_ast_free(ast->data.function_call.args[i]);
                }
                free(ast->data.function_call.args);
            }
            cxpr_ast_free_arg_names(ast->data.function_call.arg_names,
                                    ast->data.function_call.argc);
            break;
        case CXPR_NODE_LOOKBACK:
            cxpr_ast_free(ast->data.lookback.target);
            cxpr_ast_free(ast->data.lookback.index);
            break;
        case CXPR_NODE_TERNARY:
            cxpr_ast_free(ast->data.ternary.condition);
            cxpr_ast_free(ast->data.ternary.true_branch);
            cxpr_ast_free(ast->data.ternary.false_branch);
            break;
    }

    free(ast);
}
