/**
 * @file clone.c
 * @brief AST cloning helpers for evaluator flows.
 */

#include "internal.h" // IWYU pragma: keep

cxpr_expr_ast* cxpr_eval_clone_ast(const cxpr_expr_ast* ast) {
    if (!ast) return NULL;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        return cxpr_expr_ast_number_new(ast->data.number.value);
    case CXPR_NODE_BOOL:
        return cxpr_expr_ast_bool_new(ast->data.boolean.value);
    case CXPR_NODE_ARRAY: {
        cxpr_expr_ast** elements = NULL;
        if (ast->data.array.count > 0) {
            elements = (cxpr_expr_ast**)calloc(ast->data.array.count, sizeof(cxpr_expr_ast*));
            if (!elements) return NULL;
            for (size_t i = 0; i < ast->data.array.count; ++i) {
                elements[i] = cxpr_eval_clone_ast(ast->data.array.elements[i]);
                if (!elements[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_expr_ast_free(elements[j]);
                    free(elements);
                    return NULL;
                }
            }
        }
        return cxpr_expr_ast_array_new(elements, ast->data.array.count);
    }
    case CXPR_NODE_RECORD: {
        cxpr_expr_ast** values = NULL;
        if (ast->data.record.field_count > 0) {
            values = (cxpr_expr_ast**)calloc(ast->data.record.field_count, sizeof(cxpr_expr_ast*));
            if (!values) return NULL;
            for (size_t i = 0; i < ast->data.record.field_count; ++i) {
                values[i] = cxpr_eval_clone_ast(ast->data.record.field_values[i]);
                if (!values[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_expr_ast_free(values[j]);
                    free(values);
                    return NULL;
                }
            }
        }
        return cxpr_expr_ast_record_new((const char* const*)ast->data.record.field_names,
                                   values,
                                   ast->data.record.field_count);
    }
    case CXPR_NODE_STRING:
        return cxpr_expr_ast_new_string(ast->data.string.value);
    case CXPR_NODE_IDENTIFIER:
        return cxpr_expr_ast_identifier_new(ast->data.identifier.name);
    case CXPR_NODE_VARIABLE:
        return cxpr_expr_ast_param_new(ast->data.variable.name);
    case CXPR_NODE_FIELD_ACCESS:
        if (ast->data.field_access.base) {
            cxpr_expr_ast* base = cxpr_eval_clone_ast(ast->data.field_access.base);
            if (!base) return NULL;
            return cxpr_expr_ast_field_expr_new(base, ast->data.field_access.field);
        }
        return cxpr_expr_ast_field_new(ast->data.field_access.object, ast->data.field_access.field);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_expr_ast_new_chain_access((const char* const*)ast->data.chain_access.path,
                                         ast->data.chain_access.depth);
    case CXPR_NODE_UNARY_OP: {
        cxpr_expr_ast* operand = cxpr_eval_clone_ast(ast->data.unary_op.operand);
        if (!operand) return NULL;
        return cxpr_expr_ast_unary_new(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_expr_ast* left = cxpr_eval_clone_ast(ast->data.binary_op.left);
        cxpr_expr_ast* right = cxpr_eval_clone_ast(ast->data.binary_op.right);
        if (!left || !right) {
            cxpr_expr_ast_free(left);
            cxpr_expr_ast_free(right);
            return NULL;
        }
        return cxpr_expr_ast_binary_new(ast->data.binary_op.op, left, right);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_expr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.function_call.argc > 0) {
            args = (cxpr_expr_ast**)calloc(ast->data.function_call.argc, sizeof(cxpr_expr_ast*));
            arg_names = (char**)calloc(ast->data.function_call.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.function_call.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_expr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.function_call.arg_names &&
                    ast->data.function_call.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.function_call.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_expr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_expr_ast_call_named_new(ast->data.function_call.name, args,
                                                arg_names, ast->data.function_call.argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_expr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.producer_access.argc > 0) {
            args = (cxpr_expr_ast**)calloc(ast->data.producer_access.argc, sizeof(cxpr_expr_ast*));
            arg_names = (char**)calloc(ast->data.producer_access.argc, sizeof(char*));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
                args[i] = cxpr_eval_clone_ast(ast->data.producer_access.args[i]);
                if (!args[i]) {
                    for (size_t j = 0; j < i; ++j) cxpr_expr_ast_free(args[j]);
                    for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.producer_access.arg_names &&
                    ast->data.producer_access.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.producer_access.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0; j <= i; ++j) cxpr_expr_ast_free(args[j]);
                        for (size_t j = 0; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_expr_ast_producer_field_named_new(ast->data.producer_access.name, args,
                                                  arg_names, ast->data.producer_access.argc,
                                                  ast->data.producer_access.field);
    }
    case CXPR_NODE_INDEX: {
        cxpr_expr_ast* target = cxpr_eval_clone_ast(ast->data.index.target);
        cxpr_expr_ast* index = cxpr_eval_clone_ast(ast->data.index.index);
        if (!target || !index) {
            cxpr_expr_ast_free(target);
            cxpr_expr_ast_free(index);
            return NULL;
        }
        return cxpr_expr_ast_index_new(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_expr_ast* condition = cxpr_eval_clone_ast(ast->data.ternary.condition);
        cxpr_expr_ast* yes = cxpr_eval_clone_ast(ast->data.ternary.true_branch);
        cxpr_expr_ast* no = cxpr_eval_clone_ast(ast->data.ternary.false_branch);
        if (!condition || !yes || !no) {
            cxpr_expr_ast_free(condition);
            cxpr_expr_ast_free(yes);
            cxpr_expr_ast_free(no);
            return NULL;
        }
        return cxpr_expr_ast_ternary_new(condition, yes, no);
    }
    }

    return NULL;
}

static void cxpr_expr_ast_copy_spans(const cxpr_expr_ast* ast, cxpr_expr_ast* clone) {
    if (!ast || !clone) return;
    if (ast->has_source_span) {
        clone->source_span = ast->source_span;
        clone->has_source_span = true;
    }
    switch (ast->type) {
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i)
            cxpr_expr_ast_copy_spans(ast->data.array.elements[i], clone->data.array.elements[i]);
        break;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i)
            cxpr_expr_ast_copy_spans(ast->data.record.field_values[i], clone->data.record.field_values[i]);
        break;
    case CXPR_NODE_FIELD_ACCESS:
        cxpr_expr_ast_copy_spans(ast->data.field_access.base, clone->data.field_access.base);
        break;
    case CXPR_NODE_UNARY_OP:
        cxpr_expr_ast_copy_spans(ast->data.unary_op.operand, clone->data.unary_op.operand);
        break;
    case CXPR_NODE_BINARY_OP:
        cxpr_expr_ast_copy_spans(ast->data.binary_op.left, clone->data.binary_op.left);
        cxpr_expr_ast_copy_spans(ast->data.binary_op.right, clone->data.binary_op.right);
        break;
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i)
            cxpr_expr_ast_copy_spans(ast->data.function_call.args[i], clone->data.function_call.args[i]);
        break;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i)
            cxpr_expr_ast_copy_spans(ast->data.producer_access.args[i], clone->data.producer_access.args[i]);
        break;
    case CXPR_NODE_INDEX:
        cxpr_expr_ast_copy_spans(ast->data.index.target, clone->data.index.target);
        cxpr_expr_ast_copy_spans(ast->data.index.index, clone->data.index.index);
        break;
    case CXPR_NODE_TERNARY:
        cxpr_expr_ast_copy_spans(ast->data.ternary.condition, clone->data.ternary.condition);
        cxpr_expr_ast_copy_spans(ast->data.ternary.true_branch, clone->data.ternary.true_branch);
        cxpr_expr_ast_copy_spans(ast->data.ternary.false_branch, clone->data.ternary.false_branch);
        break;
    default:
        break;
    }
}

cxpr_expr_ast* cxpr_expr_ast_clone(const cxpr_expr_ast* ast) {
    cxpr_expr_ast* clone = cxpr_eval_clone_ast(ast);
    cxpr_expr_ast_copy_spans(ast, clone);
    return clone;
}
