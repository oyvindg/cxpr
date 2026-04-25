/**
 * @file parser_helpers.c
 * @brief Shared helper utilities for the recursive descent parser.
 */

#include "internal.h"
#include "../core.h"
#include <stdlib.h>
#include <string.h>

char* cxpr_parser_token_to_string(const cxpr_token* tok) {
    char* s = (char*)malloc(tok->length + 1);
    if (!s) return NULL;
    memcpy(s, tok->start, tok->length);
    s[tok->length] = '\0';
    return s;
}

cxpr_token cxpr_parser_peek_next(const cxpr_parser* p) {
    cxpr_lexer saved = p->lexer;
    return cxpr_lexer_peek(&saved);
}

cxpr_ast* cxpr_parser_clone_ast(const cxpr_ast* ast) {
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
        cxpr_ast* operand = cxpr_parser_clone_ast(ast->data.unary_op.operand);
        if (!operand) return NULL;
        return cxpr_ast_new_unary_op(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_ast* left = cxpr_parser_clone_ast(ast->data.binary_op.left);
        cxpr_ast* right = cxpr_parser_clone_ast(ast->data.binary_op.right);
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
                args[i] = cxpr_parser_clone_ast(ast->data.function_call.args[i]);
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
                args[i] = cxpr_parser_clone_ast(ast->data.producer_access.args[i]);
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
        cxpr_ast* target = cxpr_parser_clone_ast(ast->data.lookback.target);
        cxpr_ast* index = cxpr_parser_clone_ast(ast->data.lookback.index);
        if (!target || !index) {
            cxpr_ast_free(target);
            cxpr_ast_free(index);
            return NULL;
        }
        return cxpr_ast_new_lookback(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_ast* condition = cxpr_parser_clone_ast(ast->data.ternary.condition);
        cxpr_ast* yes = cxpr_parser_clone_ast(ast->data.ternary.true_branch);
        cxpr_ast* no = cxpr_parser_clone_ast(ast->data.ternary.false_branch);
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

void cxpr_parser_set_error(cxpr_parser* p, const char* message) {
    p->had_error = true;
    p->last_error.code = CXPR_ERR_SYNTAX;
    p->last_error.message = message;
    p->last_error.position = p->current.position;
    p->last_error.line = p->current.line;
    p->last_error.column = p->current.column;
}

cxpr_ast* cxpr_parser_pipe_inject_argument(cxpr_parser* p, cxpr_ast* stage, cxpr_ast* piped) {
    cxpr_ast* node = NULL;
    if (!stage || !piped) {
        cxpr_ast_free(stage);
        cxpr_ast_free(piped);
        return NULL;
    }

    switch (stage->type) {
    case CXPR_NODE_IDENTIFIER: {
        cxpr_ast** args = (cxpr_ast**)malloc(sizeof(cxpr_ast*));
        char** arg_names = (char**)calloc(1, sizeof(char*));
        if (!args || !arg_names) {
            free(args);
            free(arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        args[0] = piped;
        node = cxpr_ast_new_function_call_named(stage->data.identifier.name, args, arg_names, 1);
        if (!node) {
            free(args);
            free(arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        cxpr_ast_free(stage);
        return node;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        const size_t old_argc = stage->data.function_call.argc;
        cxpr_ast** new_args = (cxpr_ast**)malloc((old_argc + 1u) * sizeof(cxpr_ast*));
        char** new_arg_names = (char**)calloc(old_argc + 1u, sizeof(char*));
        if (!new_args || !new_arg_names) {
            free(new_args);
            free(new_arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }

        new_args[0] = piped;
        for (size_t i = 0; i < old_argc; ++i) {
            new_args[i + 1u] = stage->data.function_call.args[i];
            new_arg_names[i + 1u] = stage->data.function_call.arg_names
                                        ? stage->data.function_call.arg_names[i]
                                        : NULL;
        }
        free(stage->data.function_call.args);
        free(stage->data.function_call.arg_names);
        stage->data.function_call.args = NULL;
        stage->data.function_call.arg_names = NULL;
        stage->data.function_call.argc = 0;

        node = cxpr_ast_new_function_call_named(stage->data.function_call.name,
                                                new_args,
                                                new_arg_names,
                                                old_argc + 1u);
        cxpr_ast_free(stage);
        if (!node) {
            for (size_t i = 0; i < old_argc + 1u; ++i) {
                free(new_arg_names[i]);
                cxpr_ast_free(new_args[i]);
            }
            free(new_args);
            free(new_arg_names);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        return node;
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        const size_t old_argc = stage->data.producer_access.argc;
        cxpr_ast** new_args = (cxpr_ast**)malloc((old_argc + 1u) * sizeof(cxpr_ast*));
        char** new_arg_names = (char**)calloc(old_argc + 1u, sizeof(char*));
        if (!new_args || !new_arg_names) {
            free(new_args);
            free(new_arg_names);
            cxpr_ast_free(stage);
            cxpr_ast_free(piped);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }

        new_args[0] = piped;
        for (size_t i = 0; i < old_argc; ++i) {
            new_args[i + 1u] = stage->data.producer_access.args[i];
            new_arg_names[i + 1u] = stage->data.producer_access.arg_names
                                        ? stage->data.producer_access.arg_names[i]
                                        : NULL;
        }
        free(stage->data.producer_access.args);
        free(stage->data.producer_access.arg_names);
        stage->data.producer_access.args = NULL;
        stage->data.producer_access.arg_names = NULL;
        stage->data.producer_access.argc = 0;

        node = cxpr_ast_new_producer_access_named(stage->data.producer_access.name,
                                                  new_args,
                                                  new_arg_names,
                                                  old_argc + 1u,
                                                  stage->data.producer_access.field);
        cxpr_ast_free(stage);
        if (!node) {
            for (size_t i = 0; i < old_argc + 1u; ++i) {
                free(new_arg_names[i]);
                cxpr_ast_free(new_args[i]);
            }
            free(new_args);
            free(new_arg_names);
            p->had_error = true;
            p->last_error.code = CXPR_ERR_OUT_OF_MEMORY;
            p->last_error.message = "Out of memory";
            p->last_error.position = p->current.position;
            p->last_error.line = p->current.line;
            p->last_error.column = p->current.column;
            return NULL;
        }
        return node;
    }
    default:
        p->had_error = true;
        p->last_error.code = CXPR_ERR_SYNTAX;
        p->last_error.message = "Expected callable after '|>' (identifier or function call)";
        p->last_error.position = p->current.position;
        p->last_error.line = p->current.line;
        p->last_error.column = p->current.column;
        cxpr_ast_free(stage);
        cxpr_ast_free(piped);
        return NULL;
    }
}
