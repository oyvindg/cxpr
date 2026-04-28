/**
 * @file clone.c
 * @brief AST cloning helpers for evaluator flows.
 */

#include "internal.h" // IWYU pragma: keep

cxpr_ast* cxpr_eval_clone_ast(const cxpr_ast* ast) {
    if (!ast) return NULL;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_ast_new_number(ast->data.number.value);
    case CXPR_NODE_BOOL:
        return cxpr_ast_new_bool(ast->data.boolean.value);
    case CXPR_NODE_STRING:
        return cxpr_ast_new_string(ast->data.string.value);
    case CXPR_NODE_IDENTIFIER:
        return cxpr_ast_new_identifier(ast->data.identifier.name);
    case CXPR_NODE_VARIABLE:
        return cxpr_ast_new_variable(ast->data.variable.name);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ast_new_field_access(ast->data.field_access.object, ast->data.field_access.field);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_ast_new_chain_access((const char* const*)ast->data.chain_access.path,
                                         ast->data.chain_access.depth);
    case CXPR_NODE_UNARY_OP: {
        cxpr_ast* operand = cxpr_eval_clone_ast(ast->data.unary_op.operand);
        if (!operand) return NULL;
        return cxpr_ast_new_unary_op(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_ast* left = cxpr_eval_clone_ast(ast->data.binary_op.left);
        cxpr_ast* right = cxpr_eval_clone_ast(ast->data.binary_op.right);
        if (!left || !right) {
            cxpr_ast_free(left);
            cxpr_ast_free(right);
            return NULL;
        }
        return cxpr_ast_new_binary_op(ast->data.binary_op.op, left, right);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.function_call.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.function_call.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.function_call.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.function_call.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.function_call.arg_names &&
                    ast->data.function_call.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.function_call.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_function_call_named(ast->data.function_call.name, args,
                                                arg_names, ast->data.function_call.argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.producer_access.argc > 0) {
            args = (cxpr_ast**)calloc(ast->data.producer_access.argc, sizeof(cxpr_ast*));
            arg_names = (char**)calloc(ast->data.producer_access.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.producer_access.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.producer_access.arg_names &&
                    ast->data.producer_access.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.producer_access.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_ast_new_producer_access_named(ast->data.producer_access.name, args,
                                                  arg_names, ast->data.producer_access.argc,
                                                  ast->data.producer_access.field);
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_ast* target = cxpr_eval_clone_ast(ast->data.lookback.target);
        cxpr_ast* index = cxpr_eval_clone_ast(ast->data.lookback.index);
        if (!target || !index) {
            cxpr_ast_free(target);
            cxpr_ast_free(index);
            return NULL;
        }
        return cxpr_ast_new_lookback(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_ast* condition = cxpr_eval_clone_ast(ast->data.ternary.condition);
        cxpr_ast* yes = cxpr_eval_clone_ast(ast->data.ternary.true_branch);
        cxpr_ast* no = cxpr_eval_clone_ast(ast->data.ternary.false_branch);
        if (!condition || !yes || !no) {
            cxpr_ast_free(condition);
            cxpr_ast_free(yes);
            cxpr_ast_free(no);
            return NULL;
        }
        return cxpr_ast_new_ternary(condition, yes, no);
    }
    }

    return NULL;
}
