/**
 * @file references.c
 * @brief AST reference and usage collection.
 */

#include "internal.h"
#include <string.h>

static bool cxpr_name_in_array(const char** names, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(names[i], name) == 0) return true;
    }
    return false;
}

static size_t cxpr_add_unique_name(const char** names, size_t count, size_t max_names,
                                   const char* name) {
    size_t stored = count < max_names ? count : max_names;

    if (!name) return count;
    if (cxpr_name_in_array(names, stored, name)) return count;
    if (count < max_names) names[count] = name;
    return count + 1;
}

static bool cxpr_producer_field_in_array(const cxpr_producer_field_ref* refs,
                                         size_t count,
                                         const char* producer_name,
                                         const char* field_name) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (strcmp(refs[i].producer_name, producer_name) == 0 &&
            strcmp(refs[i].field_name, field_name) == 0) {
            return true;
        }
    }
    return false;
}

static size_t cxpr_add_unique_producer_field(cxpr_producer_field_ref* refs,
                                             size_t count,
                                             size_t max_refs,
                                             const char* producer_name,
                                             const char* field_name) {
    size_t stored = count < max_refs ? count : max_refs;

    if (!producer_name || !field_name) return count;
    if (cxpr_producer_field_in_array(refs, stored, producer_name, field_name)) return count;
    if (count < max_refs) {
        refs[count].producer_name = producer_name;
        refs[count].field_name = field_name;
    }
    return count + 1;
}

static size_t cxpr_collect_references(const cxpr_ast* ast, const char** names,
                                      size_t count, size_t max_names) {
    const char* full_ref;

    if (!ast) return count;

    full_ref = cxpr_ast_full_reference(ast);
    if (full_ref) return cxpr_add_unique_name(names, count, max_names, full_ref);

    switch (ast->type) {
        case CXPR_NODE_IDENTIFIER:
            return cxpr_add_unique_name(names, count, max_names, ast->data.identifier.name);
        case CXPR_NODE_BINARY_OP:
            count = cxpr_collect_references(ast->data.binary_op.left, names, count, max_names);
            return cxpr_collect_references(ast->data.binary_op.right, names, count, max_names);
        case CXPR_NODE_UNARY_OP:
            return cxpr_collect_references(ast->data.unary_op.operand, names, count, max_names);
        case CXPR_NODE_FUNCTION_CALL:
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                count = cxpr_collect_references(ast->data.function_call.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_PRODUCER_ACCESS:
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                count = cxpr_collect_references(ast->data.producer_access.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_LOOKBACK:
            count = cxpr_collect_references(ast->data.lookback.target, names, count, max_names);
            return cxpr_collect_references(ast->data.lookback.index, names, count, max_names);
        case CXPR_NODE_TERNARY:
            count = cxpr_collect_references(ast->data.ternary.condition, names, count, max_names);
            count = cxpr_collect_references(ast->data.ternary.true_branch, names, count, max_names);
            return cxpr_collect_references(ast->data.ternary.false_branch, names, count, max_names);
        default:
            return count;
    }
}

static size_t cxpr_collect_functions(const cxpr_ast* ast, const char** names,
                                     size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
        case CXPR_NODE_FUNCTION_CALL:
            count = cxpr_add_unique_name(names, count, max_names, ast->data.function_call.name);
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                count = cxpr_collect_functions(ast->data.function_call.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_PRODUCER_ACCESS:
            count = cxpr_add_unique_name(names, count, max_names, ast->data.producer_access.name);
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                count = cxpr_collect_functions(ast->data.producer_access.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_BINARY_OP:
            count = cxpr_collect_functions(ast->data.binary_op.left, names, count, max_names);
            return cxpr_collect_functions(ast->data.binary_op.right, names, count, max_names);
        case CXPR_NODE_UNARY_OP:
            return cxpr_collect_functions(ast->data.unary_op.operand, names, count, max_names);
        case CXPR_NODE_LOOKBACK:
            count = cxpr_collect_functions(ast->data.lookback.target, names, count, max_names);
            return cxpr_collect_functions(ast->data.lookback.index, names, count, max_names);
        case CXPR_NODE_TERNARY:
            count = cxpr_collect_functions(ast->data.ternary.condition, names, count, max_names);
            count = cxpr_collect_functions(ast->data.ternary.true_branch, names, count, max_names);
            return cxpr_collect_functions(ast->data.ternary.false_branch, names, count, max_names);
        default:
            return count;
    }
}

static size_t cxpr_collect_variables(const cxpr_ast* ast, const char** names,
                                     size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
        case CXPR_NODE_VARIABLE:
            return cxpr_add_unique_name(names, count, max_names, ast->data.variable.name);
        case CXPR_NODE_BINARY_OP:
            count = cxpr_collect_variables(ast->data.binary_op.left, names, count, max_names);
            return cxpr_collect_variables(ast->data.binary_op.right, names, count, max_names);
        case CXPR_NODE_UNARY_OP:
            return cxpr_collect_variables(ast->data.unary_op.operand, names, count, max_names);
        case CXPR_NODE_FUNCTION_CALL:
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                count = cxpr_collect_variables(ast->data.function_call.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_PRODUCER_ACCESS:
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                count = cxpr_collect_variables(ast->data.producer_access.args[i], names, count, max_names);
            }
            return count;
        case CXPR_NODE_LOOKBACK:
            count = cxpr_collect_variables(ast->data.lookback.target, names, count, max_names);
            return cxpr_collect_variables(ast->data.lookback.index, names, count, max_names);
        case CXPR_NODE_TERNARY:
            count = cxpr_collect_variables(ast->data.ternary.condition, names, count, max_names);
            count = cxpr_collect_variables(ast->data.ternary.true_branch, names, count, max_names);
            return cxpr_collect_variables(ast->data.ternary.false_branch, names, count, max_names);
        default:
            return count;
    }
}

static size_t cxpr_collect_producer_fields(const cxpr_ast* ast,
                                           cxpr_producer_field_ref* refs,
                                           size_t count,
                                           size_t max_refs) {
    if (!ast) return count;

    switch (ast->type) {
        case CXPR_NODE_PRODUCER_ACCESS:
            count = cxpr_add_unique_producer_field(
                refs,
                count,
                max_refs,
                ast->data.producer_access.name,
                ast->data.producer_access.field);
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                count = cxpr_collect_producer_fields(
                    ast->data.producer_access.args[i],
                    refs,
                    count,
                    max_refs);
            }
            return count;
        case CXPR_NODE_FUNCTION_CALL:
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                count = cxpr_collect_producer_fields(
                    ast->data.function_call.args[i],
                    refs,
                    count,
                    max_refs);
            }
            return count;
        case CXPR_NODE_BINARY_OP:
            count = cxpr_collect_producer_fields(ast->data.binary_op.left, refs, count, max_refs);
            return cxpr_collect_producer_fields(ast->data.binary_op.right, refs, count, max_refs);
        case CXPR_NODE_UNARY_OP:
            return cxpr_collect_producer_fields(ast->data.unary_op.operand, refs, count, max_refs);
        case CXPR_NODE_LOOKBACK:
            count = cxpr_collect_producer_fields(ast->data.lookback.target, refs, count, max_refs);
            return cxpr_collect_producer_fields(ast->data.lookback.index, refs, count, max_refs);
        case CXPR_NODE_TERNARY:
            count = cxpr_collect_producer_fields(ast->data.ternary.condition, refs, count, max_refs);
            count = cxpr_collect_producer_fields(ast->data.ternary.true_branch, refs, count, max_refs);
            return cxpr_collect_producer_fields(ast->data.ternary.false_branch, refs, count, max_refs);
        default:
            return count;
    }
}

size_t cxpr_ast_references(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_references(ast, names, 0, max_names);
}

size_t cxpr_ast_functions_used(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_functions(ast, names, 0, max_names);
}

size_t cxpr_ast_producer_fields_used(const cxpr_ast* ast,
                                     cxpr_producer_field_ref* refs,
                                     size_t max_refs) {
    return cxpr_collect_producer_fields(ast, refs, 0, max_refs);
}

size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_variables(ast, names, 0, max_names);
}
