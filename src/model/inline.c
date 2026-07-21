#include "model/internal.h"
#include <stdlib.h>

const cxpr_ast* cxpr_model_local_lookup(const cxpr_model_local_binding* locals,
                                        size_t count,
                                        const char* name) {
    for (size_t i = count; i > 0; --i) {
        if (cxpr_model_names_match(locals[i - 1].name, name)) return locals[i - 1].expr;
    }
    return NULL;
}

static char** cxpr_model_clone_arg_names_from_call(const cxpr_ast* ast, size_t argc) {
    char** names = NULL;
    if (argc == 0u) return NULL;
    names = (char**)calloc(argc, sizeof(char*));
    if (!names) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const char* name = cxpr_ast_function_arg_name(ast, i);
        if (name) {
            names[i] = cxpr_strdup(name);
            if (!names[i]) {
                for (size_t j = 0; j < i; ++j) free(names[j]);
                free(names);
                return NULL;
            }
        }
    }
    return names;
}

static char** cxpr_model_clone_arg_names_from_producer(const cxpr_ast* ast, size_t argc) {
    char** names = NULL;
    if (argc == 0u) return NULL;
    names = (char**)calloc(argc, sizeof(char*));
    if (!names) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const char* name = cxpr_ast_producer_arg_name(ast, i);
        if (name) {
            names[i] = cxpr_strdup(name);
            if (!names[i]) {
                for (size_t j = 0; j < i; ++j) free(names[j]);
                free(names);
                return NULL;
            }
        }
    }
    return names;
}

static cxpr_ast** cxpr_model_inline_args(const cxpr_ast* ast,
                                         size_t argc,
                                         const cxpr_model_local_binding* locals,
                                         size_t local_count,
                                         bool producer) {
    cxpr_ast** args = NULL;
    if (argc == 0u) return NULL;
    args = (cxpr_ast**)calloc(argc, sizeof(cxpr_ast*));
    if (!args) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const cxpr_ast* arg = producer ? cxpr_ast_producer_arg(ast, i)
                                       : cxpr_ast_function_arg(ast, i);
        args[i] = cxpr_model_inline_locals(arg, locals, local_count);
        if (!args[i]) {
            for (size_t j = 0; j < i; ++j) cxpr_ast_free(args[j]);
            free(args);
            return NULL;
        }
    }
    return args;
}

cxpr_ast* cxpr_model_inline_locals(const cxpr_ast* ast,
                                   const cxpr_model_local_binding* locals,
                                   size_t local_count) {
    cxpr_node_type type;
    if (!ast) return NULL;
    type = cxpr_ast_type(ast);

    if (type == CXPR_NODE_IDENTIFIER) {
        const cxpr_ast* replacement =
            cxpr_model_local_lookup(locals, local_count, cxpr_ast_identifier_name(ast));
        if (replacement) return cxpr_ast_clone(replacement);
        return cxpr_ast_clone(ast);
    }

    switch (type) {
    case CXPR_NODE_RECORD: {
        const size_t field_count = cxpr_ast_record_field_count(ast);
        cxpr_ast** values = NULL;
        const char** names = NULL;
        cxpr_model_local_binding* scoped_locals = NULL;
        if (field_count > 0u) {
            values = (cxpr_ast**)calloc(field_count, sizeof(cxpr_ast*));
            names = (const char**)calloc(field_count, sizeof(char*));
            scoped_locals = (cxpr_model_local_binding*)calloc(local_count + field_count,
                                                              sizeof(cxpr_model_local_binding));
            if (!values || !names || !scoped_locals) {
                free(values);
                free(names);
                free(scoped_locals);
                return NULL;
            }
            for (size_t i = 0u; i < local_count; ++i) scoped_locals[i] = locals[i];
            for (size_t i = 0u; i < field_count; ++i) {
                names[i] = cxpr_ast_record_field_name(ast, i);
                values[i] = cxpr_model_inline_locals(
                    cxpr_ast_record_field_value(ast, i), scoped_locals, local_count + i);
                if (!values[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_ast_free(values[j]);
                    free(values);
                    free(names);
                    free(scoped_locals);
                    return NULL;
                }
                scoped_locals[local_count + i].name = (char*)names[i];
                scoped_locals[local_count + i].expr = values[i];
            }
        }
        {
            cxpr_ast* record = cxpr_ast_new_record(names, values, field_count);
            free(names);
            free(scoped_locals);
            if (!record) {
                for (size_t i = 0u; i < field_count; ++i) cxpr_ast_free(values[i]);
                free(values);
            }
            return record;
        }
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_ast* left = cxpr_model_inline_locals(cxpr_ast_left(ast), locals, local_count);
        cxpr_ast* right = cxpr_model_inline_locals(cxpr_ast_right(ast), locals, local_count);
        if (!left || !right) {
            cxpr_ast_free(left);
            cxpr_ast_free(right);
            return NULL;
        }
        return cxpr_ast_new_binary_op(cxpr_ast_operator(ast), left, right);
    }
    case CXPR_NODE_UNARY_OP: {
        cxpr_ast* operand = cxpr_model_inline_locals(cxpr_ast_operand(ast), locals, local_count);
        if (!operand) return NULL;
        return cxpr_ast_new_unary_op(cxpr_ast_operator(ast), operand);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        size_t argc = cxpr_ast_function_argc(ast);
        cxpr_ast** args = cxpr_model_inline_args(ast, argc, locals, local_count, false);
        char** arg_names = cxpr_model_clone_arg_names_from_call(ast, argc);
        if (argc > 0u && (!args || (cxpr_ast_function_has_named_args(ast) && !arg_names))) {
            if (args) {
                for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                free(args);
            }
            return NULL;
        }
        return cxpr_ast_new_function_call_named(cxpr_ast_function_name(ast), args, arg_names, argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        size_t argc = cxpr_ast_producer_argc(ast);
        cxpr_ast** args = cxpr_model_inline_args(ast, argc, locals, local_count, true);
        char** arg_names = cxpr_model_clone_arg_names_from_producer(ast, argc);
        if (argc > 0u && (!args || (cxpr_ast_producer_has_named_args(ast) && !arg_names))) {
            if (args) {
                for (size_t i = 0; i < argc; ++i) cxpr_ast_free(args[i]);
                free(args);
            }
            return NULL;
        }
        return cxpr_ast_new_producer_access_named(cxpr_ast_producer_name(ast), args, arg_names,
                                                  argc, cxpr_ast_producer_field(ast));
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_ast* target = cxpr_model_inline_locals(cxpr_ast_lookback_target(ast), locals, local_count);
        cxpr_ast* index = cxpr_model_inline_locals(cxpr_ast_lookback_index(ast), locals, local_count);
        if (!target || !index) {
            cxpr_ast_free(target);
            cxpr_ast_free(index);
            return NULL;
        }
        return cxpr_ast_new_lookback(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_ast* condition = cxpr_model_inline_locals(cxpr_ast_ternary_condition(ast), locals, local_count);
        cxpr_ast* yes = cxpr_model_inline_locals(cxpr_ast_ternary_true_branch(ast), locals, local_count);
        cxpr_ast* no = cxpr_model_inline_locals(cxpr_ast_ternary_false_branch(ast), locals, local_count);
        if (!condition || !yes || !no) {
            cxpr_ast_free(condition);
            cxpr_ast_free(yes);
            cxpr_ast_free(no);
            return NULL;
        }
        return cxpr_ast_new_ternary(condition, yes, no);
    }
    default:
        return cxpr_ast_clone(ast);
    }
}
