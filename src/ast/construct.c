/**
 * @file construct.c
 * @brief AST node constructors.
 */

#include "internal.h"
#include "core.h"
#include <stdlib.h>
#include <string.h>

static char* cxpr_ast_join_with_dot(const char* left, const char* right) {
    size_t left_len;
    size_t right_len;
    char* out;

    if (!left || !right) return NULL;
    left_len = strlen(left);
    right_len = strlen(right);
    out = (char*)malloc(left_len + right_len + 2);
    if (!out) return NULL;
    memcpy(out, left, left_len);
    out[left_len] = '.';
    memcpy(out + left_len + 1, right, right_len);
    out[left_len + right_len + 1] = '\0';
    return out;
}

cxpr_ast* cxpr_ast_new_number(double value) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_NUMBER;
    node->data.number.value = value;
    return node;
}

cxpr_ast* cxpr_ast_new_bool(bool value) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_BOOL;
    node->data.boolean.value = value;
    return node;
}

cxpr_ast* cxpr_ast_new_string(const char* value) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_STRING;
    node->data.string.value = cxpr_strdup(value ? value : "");
    if (!node->data.string.value) {
        free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_identifier(const char* name) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_IDENTIFIER;
    node->data.identifier.name = cxpr_strdup(name);
    if (!node->data.identifier.name) {
        free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_variable(const char* name) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_VARIABLE;
    node->data.variable.name = cxpr_strdup(name);
    if (!node->data.variable.name) {
        free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_FIELD_ACCESS;
    node->data.field_access.object = cxpr_strdup(object);
    node->data.field_access.field = cxpr_strdup(field);
    node->data.field_access.full_key = cxpr_ast_join_with_dot(object, field);
    if (!node->data.field_access.object || !node->data.field_access.field ||
        !node->data.field_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_chain_access(const char* const* path, size_t depth) {
    cxpr_ast* node;
    size_t total_len = 1;
    char* cursor;

    if (!path || depth < 2) return NULL;

    node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_CHAIN_ACCESS;
    node->data.chain_access.path = (char**)calloc(depth, sizeof(char*));
    if (!node->data.chain_access.path) {
        free(node);
        return NULL;
    }
    node->data.chain_access.depth = depth;

    for (size_t i = 0; i < depth; ++i) {
        node->data.chain_access.path[i] = cxpr_strdup(path[i]);
        if (!node->data.chain_access.path[i]) {
            cxpr_ast_free(node);
            return NULL;
        }
        total_len += strlen(path[i]) + (i + 1 < depth ? 1 : 0);
    }

    node->data.chain_access.full_key = (char*)malloc(total_len);
    if (!node->data.chain_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }

    cursor = node->data.chain_access.full_key;
    for (size_t i = 0; i < depth; ++i) {
        size_t len = strlen(path[i]);
        memcpy(cursor, path[i], len);
        cursor += len;
        if (i + 1 < depth) *cursor++ = '.';
    }
    *cursor = '\0';
    return node;
}

cxpr_ast* cxpr_ast_new_producer_access_named(const char* name, cxpr_ast** args,
                                             char** arg_names, size_t argc,
                                             const char* field) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_PRODUCER_ACCESS;
    node->data.producer_access.name = cxpr_strdup(name);
    node->data.producer_access.args = args;
    node->data.producer_access.arg_names = arg_names;
    node->data.producer_access.argc = argc;
    node->data.producer_access.field = cxpr_strdup(field);
    node->data.producer_access.full_key = cxpr_ast_join_with_dot(name, field);
    if (!node->data.producer_access.name || !node->data.producer_access.field ||
        !node->data.producer_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args, size_t argc,
                                       const char* field) {
    return cxpr_ast_new_producer_access_named(name, args, NULL, argc, field);
}

cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_BINARY_OP;
    node->data.binary_op.op = op;
    node->data.binary_op.left = left;
    node->data.binary_op.right = right;
    return node;
}

cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_UNARY_OP;
    node->data.unary_op.op = op;
    node->data.unary_op.operand = operand;
    return node;
}

cxpr_ast* cxpr_ast_new_function_call_named(const char* name, cxpr_ast** args,
                                           char** arg_names, size_t argc) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_FUNCTION_CALL;
    node->data.function_call.name = cxpr_strdup(name);
    node->data.function_call.args = args;
    node->data.function_call.arg_names = arg_names;
    node->data.function_call.argc = argc;
    if (!node->data.function_call.name) {
        cxpr_ast_free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc) {
    return cxpr_ast_new_function_call_named(name, args, NULL, argc);
}

cxpr_ast* cxpr_ast_new_lookback(cxpr_ast* target, cxpr_ast* index) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_LOOKBACK;
    node->data.lookback.target = target;
    node->data.lookback.index = index;
    return node;
}

cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch, cxpr_ast* false_branch) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_TERNARY;
    node->data.ternary.condition = condition;
    node->data.ternary.true_branch = true_branch;
    node->data.ternary.false_branch = false_branch;
    return node;
}
