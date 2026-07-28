/**
 * @file alias.c
 * @brief AST-level expression alias expansion.
 */

#include "internal.h"
#include "core.h"

#include <cxpr/alias.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const cxpr_alias* aliases;
    size_t alias_count;
    const char** stack;
    size_t stack_count;
    size_t stack_cap;
    cxpr_error* err;
} cxpr_alias_expand_ctx;

static void cxpr_alias_set_error(cxpr_alias_expand_ctx* ctx, const char* message, size_t pos) {
    if (!ctx || !ctx->err) return;
    ctx->err->message = message;
    ctx->err->position = pos;
}

static const char* cxpr_alias_lookup(cxpr_alias_expand_ctx* ctx, const char* name) {
    size_t i;

    if (!ctx || !name || name[0] == '\0') return NULL;
    for (i = 0u; i < ctx->alias_count; ++i) {
        if (ctx->aliases[i].name &&
            ctx->aliases[i].expression &&
            strcmp(ctx->aliases[i].name, name) == 0) {
            return ctx->aliases[i].expression;
        }
    }
    return NULL;
}

static int cxpr_alias_stack_contains(cxpr_alias_expand_ctx* ctx, const char* name) {
    size_t i;

    if (!ctx || !name) return 0;
    for (i = 0u; i < ctx->stack_count; ++i) {
        if (ctx->stack[i] && strcmp(ctx->stack[i], name) == 0) return 1;
    }
    return 0;
}

static int cxpr_alias_stack_push(cxpr_alias_expand_ctx* ctx, const char* name) {
    const char** grown;
    size_t cap;

    if (!ctx || !name) return 0;
    if (cxpr_alias_stack_contains(ctx, name)) {
        cxpr_alias_set_error(ctx, "Expression alias cycle", 0u);
        return 0;
    }
    if (ctx->stack_count == ctx->stack_cap) {
        cap = ctx->stack_cap ? ctx->stack_cap * 2u : 8u;
        grown = (const char**)realloc(ctx->stack, cap * sizeof(*grown));
        if (!grown) {
            cxpr_alias_set_error(ctx, "Out of memory", 0u);
            return 0;
        }
        ctx->stack = grown;
        ctx->stack_cap = cap;
    }
    ctx->stack[ctx->stack_count++] = name;
    return 1;
}

static void cxpr_alias_stack_pop(cxpr_alias_expand_ctx* ctx) {
    if (!ctx || ctx->stack_count == 0u) return;
    --ctx->stack_count;
}

static char* cxpr_alias_join2(const char* left, const char* right) {
    size_t left_len;
    size_t right_len;
    char* out;

    if (!left || !right) return NULL;
    left_len = strlen(left);
    right_len = strlen(right);
    out = (char*)malloc(left_len + right_len + 2u);
    if (!out) return NULL;
    memcpy(out, left, left_len);
    out[left_len] = '.';
    memcpy(out + left_len + 1u, right, right_len);
    out[left_len + right_len + 1u] = '\0';
    return out;
}

static char* cxpr_alias_join_path(char* const* path, size_t start, size_t depth) {
    size_t total = 1u;
    size_t i;
    char* out;
    char* cursor;

    if (!path || start >= depth) return NULL;
    for (i = start; i < depth; ++i) total += strlen(path[i]) + (i + 1u < depth ? 1u : 0u);
    out = (char*)malloc(total);
    if (!out) return NULL;
    cursor = out;
    for (i = start; i < depth; ++i) {
        size_t len = strlen(path[i]);
        memcpy(cursor, path[i], len);
        cursor += len;
        if (i + 1u < depth) *cursor++ = '.';
    }
    *cursor = '\0';
    return out;
}

static char** cxpr_alias_clone_arg_names(char* const* names, size_t argc) {
    char** out;
    size_t i;

    if (!names || argc == 0u) return NULL;
    out = (char**)calloc(argc, sizeof(*out));
    if (!out) return NULL;
    for (i = 0u; i < argc; ++i) {
        if (!names[i]) continue;
        out[i] = cxpr_strdup(names[i]);
        if (!out[i]) {
            while (i > 0u) free(out[--i]);
            free(out);
            return NULL;
        }
    }
    return out;
}

static cxpr_expr_ast** cxpr_alias_expand_args(cxpr_alias_expand_ctx* ctx,
                                         cxpr_expr_ast* const* args,
                                         size_t argc);
static cxpr_expr_ast* cxpr_alias_expand_ast(cxpr_alias_expand_ctx* ctx, const cxpr_expr_ast* ast);
static cxpr_expr_ast* cxpr_alias_expand_named(cxpr_alias_expand_ctx* ctx, const char* name);

static cxpr_expr_ast* cxpr_alias_make_field_from_expanded(cxpr_expr_ast* expanded, const char* field) {
    cxpr_expr_ast** args;
    char** arg_names;
    cxpr_expr_ast* out;

    if (!expanded || !field) return NULL;
    if (expanded->type == CXPR_NODE_FUNCTION_CALL) {
        args = NULL;
        arg_names = NULL;
        if (expanded->data.function_call.argc > 0u) {
            args = (cxpr_expr_ast**)calloc(expanded->data.function_call.argc, sizeof(*args));
            if (!args) return NULL;
            for (size_t i = 0u; i < expanded->data.function_call.argc; ++i) {
                args[i] = cxpr_expr_ast_clone(expanded->data.function_call.args[i]);
                if (!args[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(args[j]);
                    free(args);
                    return NULL;
                }
            }
            arg_names = cxpr_alias_clone_arg_names(
                expanded->data.function_call.arg_names,
                expanded->data.function_call.argc);
            if (expanded->data.function_call.argc > 0u &&
                expanded->data.function_call.arg_names && !arg_names) {
                for (size_t i = 0u; i < expanded->data.function_call.argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
                return NULL;
            }
        }
        out = cxpr_expr_ast_producer_field_named_new(
            expanded->data.function_call.name,
            args,
            arg_names,
            expanded->data.function_call.argc,
            field);
        if (!out) {
            if (args) {
                for (size_t i = 0u; i < expanded->data.function_call.argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
            }
            if (arg_names) {
                for (size_t i = 0u; i < expanded->data.function_call.argc; ++i) free(arg_names[i]);
                free(arg_names);
            }
        }
        return out;
    }
    return NULL;
}

static cxpr_expr_ast** cxpr_alias_expand_args(cxpr_alias_expand_ctx* ctx,
                                         cxpr_expr_ast* const* args,
                                         size_t argc) {
    cxpr_expr_ast** out;
    size_t i;

    if (argc == 0u) return NULL;
    out = (cxpr_expr_ast**)calloc(argc, sizeof(*out));
    if (!out) {
        cxpr_alias_set_error(ctx, "Out of memory", 0u);
        return NULL;
    }
    for (i = 0u; i < argc; ++i) {
        out[i] = cxpr_alias_expand_ast(ctx, args[i]);
        if (!out[i]) {
            while (i > 0u) cxpr_expr_ast_free(out[--i]);
            free(out);
            return NULL;
        }
    }
    return out;
}

static cxpr_expr_ast* cxpr_alias_expand_named(cxpr_alias_expand_ctx* ctx, const char* name) {
    const char* expression;
    cxpr_parser* parser;
    cxpr_expr_ast* ast = NULL;
    cxpr_expr_ast* expanded = NULL;
    cxpr_error parse_err = {0};

    expression = cxpr_alias_lookup(ctx, name);
    if (!expression) return NULL;
    if (!cxpr_alias_stack_push(ctx, name)) return NULL;
    parser = cxpr_parser_new();
    if (!parser) {
        cxpr_alias_set_error(ctx, "Out of memory", 0u);
        goto cleanup;
    }
    ast = cxpr_expr_ast_parse(parser, expression, &parse_err);
    if (!ast) {
        if (ctx && ctx->err) *ctx->err = parse_err;
        goto cleanup;
    }
    expanded = cxpr_alias_expand_ast(ctx, ast);

cleanup:
    cxpr_expr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_alias_stack_pop(ctx);
    return expanded;
}

static cxpr_expr_ast* cxpr_alias_expand_field(cxpr_alias_expand_ctx* ctx, const cxpr_expr_ast* ast) {
    char* full_key;
    const char* alias_expr;
    cxpr_expr_ast* expanded;
    cxpr_expr_ast* out;

    if (ast->data.field_access.base) {
        expanded = cxpr_alias_expand_ast(ctx, ast->data.field_access.base);
        if (!expanded) return NULL;
        out = cxpr_expr_ast_field_expr_new(expanded, ast->data.field_access.field);
        if (!out) cxpr_expr_ast_free(expanded);
        return out;
    }

    full_key = cxpr_alias_join2(ast->data.field_access.object, ast->data.field_access.field);
    if (!full_key) {
        cxpr_alias_set_error(ctx, "Out of memory", 0u);
        return NULL;
    }
    alias_expr = cxpr_alias_lookup(ctx, full_key);
    if (alias_expr) {
        out = cxpr_alias_expand_named(ctx, full_key);
        free(full_key);
        return out;
    }
    free(full_key);

    if (cxpr_alias_lookup(ctx, ast->data.field_access.object)) {
        expanded = cxpr_alias_expand_named(ctx, ast->data.field_access.object);
        if (!expanded) return NULL;
        out = cxpr_alias_make_field_from_expanded(expanded, ast->data.field_access.field);
        cxpr_expr_ast_free(expanded);
        if (out) return out;
    }
    return cxpr_expr_ast_clone(ast);
}

static cxpr_expr_ast* cxpr_alias_expand_chain(cxpr_alias_expand_ctx* ctx, const cxpr_expr_ast* ast) {
    char* full_key = NULL;
    char* tail = NULL;
    cxpr_expr_ast* expanded = NULL;
    cxpr_expr_ast* out = NULL;

    full_key = cxpr_alias_join_path(ast->data.chain_access.path, 0u, ast->data.chain_access.depth);
    if (!full_key) {
        cxpr_alias_set_error(ctx, "Out of memory", 0u);
        return NULL;
    }
    if (cxpr_alias_lookup(ctx, full_key)) {
        out = cxpr_alias_expand_named(ctx, full_key);
        free(full_key);
        return out;
    }
    free(full_key);

    if (cxpr_alias_lookup(ctx, ast->data.chain_access.path[0])) {
        expanded = cxpr_alias_expand_named(ctx, ast->data.chain_access.path[0]);
        if (!expanded) return NULL;
        tail = cxpr_alias_join_path(ast->data.chain_access.path, 1u, ast->data.chain_access.depth);
        if (!tail) {
            cxpr_expr_ast_free(expanded);
            cxpr_alias_set_error(ctx, "Out of memory", 0u);
            return NULL;
        }
        out = cxpr_alias_make_field_from_expanded(expanded, tail);
        free(tail);
        cxpr_expr_ast_free(expanded);
        if (out) return out;
    }
    return cxpr_expr_ast_clone(ast);
}

static cxpr_expr_ast* cxpr_alias_expand_ast(cxpr_alias_expand_ctx* ctx, const cxpr_expr_ast* ast) {
    cxpr_expr_ast** args;
    char** arg_names;

    if (!ast) return NULL;
    switch (ast->type) {
        case CXPR_NODE_IDENTIFIER: {
            if (cxpr_alias_lookup(ctx, ast->data.identifier.name)) {
                return cxpr_alias_expand_named(ctx, ast->data.identifier.name);
            }
            return cxpr_expr_ast_clone(ast);
        }
        case CXPR_NODE_FIELD_ACCESS:
            return cxpr_alias_expand_field(ctx, ast);
        case CXPR_NODE_CHAIN_ACCESS:
            return cxpr_alias_expand_chain(ctx, ast);
        case CXPR_NODE_BINARY_OP:
        {
            cxpr_expr_ast* left = cxpr_alias_expand_ast(ctx, ast->data.binary_op.left);
            cxpr_expr_ast* right = left ? cxpr_alias_expand_ast(ctx, ast->data.binary_op.right) : NULL;
            if (!left || !right) {
                cxpr_expr_ast_free(left);
                cxpr_expr_ast_free(right);
                return NULL;
            }
            return cxpr_expr_ast_binary_new(ast->data.binary_op.op, left, right);
        }
        case CXPR_NODE_UNARY_OP:
        {
            cxpr_expr_ast* operand = cxpr_alias_expand_ast(ctx, ast->data.unary_op.operand);
            if (!operand) return NULL;
            return cxpr_expr_ast_unary_new(ast->data.unary_op.op, operand);
        }
        case CXPR_NODE_FUNCTION_CALL:
        {
            cxpr_expr_ast* out_call;
            args = cxpr_alias_expand_args(ctx, ast->data.function_call.args, ast->data.function_call.argc);
            if (ast->data.function_call.argc > 0u && !args) {
                return NULL;
            }
            arg_names = cxpr_alias_clone_arg_names(ast->data.function_call.arg_names, ast->data.function_call.argc);
            if (ast->data.function_call.argc > 0u && ast->data.function_call.arg_names && !arg_names) {
                for (size_t i = 0u; i < ast->data.function_call.argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
                cxpr_alias_set_error(ctx, "Out of memory", 0u);
                return NULL;
            }
            out_call = cxpr_expr_ast_call_named_new(
                ast->data.function_call.name,
                args,
                arg_names,
                ast->data.function_call.argc);
            return out_call;
        }
        case CXPR_NODE_PRODUCER_ACCESS:
            args = cxpr_alias_expand_args(ctx, ast->data.producer_access.args, ast->data.producer_access.argc);
            if (ast->data.producer_access.argc > 0u && !args) return NULL;
            arg_names = cxpr_alias_clone_arg_names(ast->data.producer_access.arg_names, ast->data.producer_access.argc);
            if (ast->data.producer_access.argc > 0u && ast->data.producer_access.arg_names && !arg_names) {
                for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
                cxpr_alias_set_error(ctx, "Out of memory", 0u);
                return NULL;
            }
            return cxpr_expr_ast_producer_field_named_new(
                ast->data.producer_access.name,
                args,
                arg_names,
                ast->data.producer_access.argc,
                ast->data.producer_access.field);
        case CXPR_NODE_LOOKBACK:
        {
            cxpr_expr_ast* target = cxpr_alias_expand_ast(ctx, ast->data.lookback.target);
            cxpr_expr_ast* index = target ? cxpr_alias_expand_ast(ctx, ast->data.lookback.index) : NULL;
            if (!target || !index) {
                cxpr_expr_ast_free(target);
                cxpr_expr_ast_free(index);
                return NULL;
            }
            return cxpr_expr_ast_lookback_new(target, index);
        }
        case CXPR_NODE_TERNARY:
        {
            cxpr_expr_ast* condition = cxpr_alias_expand_ast(ctx, ast->data.ternary.condition);
            cxpr_expr_ast* true_branch = condition ? cxpr_alias_expand_ast(ctx, ast->data.ternary.true_branch) : NULL;
            cxpr_expr_ast* false_branch = true_branch ? cxpr_alias_expand_ast(ctx, ast->data.ternary.false_branch) : NULL;
            if (!condition || !true_branch || !false_branch) {
                cxpr_expr_ast_free(condition);
                cxpr_expr_ast_free(true_branch);
                cxpr_expr_ast_free(false_branch);
                return NULL;
            }
            return cxpr_expr_ast_ternary_new(condition, true_branch, false_branch);
        }
        default:
            return cxpr_expr_ast_clone(ast);
    }
}

int cxpr_expand_aliases(const char* expression,
                        const cxpr_alias* aliases,
                        size_t alias_count,
                        char** out_expression,
                        cxpr_error* err) {
    cxpr_parser* parser = NULL;
    cxpr_expr_ast* ast = NULL;
    cxpr_expr_ast* expanded = NULL;
    cxpr_alias_expand_ctx ctx = {0};
    int ok = 0;

    if (err) *err = (cxpr_error){0};
    if (!out_expression) return 0;
    *out_expression = NULL;
    parser = cxpr_parser_new();
    if (!parser) {
        if (err) {
            err->message = "Out of memory";
            err->position = 0u;
        }
        return 0;
    }
    ast = cxpr_expr_ast_parse(parser, expression ? expression : "", err);
    if (!ast) goto cleanup;

    ctx.aliases = aliases;
    ctx.alias_count = alias_count;
    ctx.err = err;
    expanded = cxpr_alias_expand_ast(&ctx, ast);
    if (!expanded) goto cleanup;
    *out_expression = cxpr_expr_ast_to_string(expanded);
    ok = (*out_expression != NULL);
    if (!ok && err) {
        err->message = "Out of memory";
        err->position = 0u;
    }

cleanup:
    free(ctx.stack);
    cxpr_expr_ast_free(expanded);
    cxpr_expr_ast_free(ast);
    cxpr_parser_free(parser);
    return ok;
}
