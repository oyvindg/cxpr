#include "model/internal.h"
#include "model/window/window.h"
#include <stdlib.h>

const cxpr_expr_ast* cxpr_model_local_lookup(const cxpr_model_local_binding* locals,
                                        size_t count,
                                        const char* name) {
    for (size_t i = count; i > 0; --i) {
        if (cxpr_model_names_match(locals[i - 1].name, name)) return locals[i - 1].expr;
    }
    return NULL;
}

static char** cxpr_model_clone_arg_names_from_call(const cxpr_expr_ast* ast, size_t argc) {
    char** names = NULL;
    if (argc == 0u) return NULL;
    names = (char**)calloc(argc, sizeof(char*));
    if (!names) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const char* name = cxpr_expr_ast_call_arg_name(ast, i);
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

static char** cxpr_model_clone_arg_names_from_producer(const cxpr_expr_ast* ast, size_t argc) {
    char** names = NULL;
    if (argc == 0u) return NULL;
    names = (char**)calloc(argc, sizeof(char*));
    if (!names) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const char* name = cxpr_expr_ast_producer_arg_name(ast, i);
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

static cxpr_expr_ast** cxpr_model_inline_args(const cxpr_expr_ast* ast,
                                         size_t argc,
                                         const cxpr_model_local_binding* locals,
                                         size_t local_count,
                                         bool producer) {
    cxpr_expr_ast** args = NULL;
    if (argc == 0u) return NULL;
    args = (cxpr_expr_ast**)calloc(argc, sizeof(cxpr_expr_ast*));
    if (!args) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        const cxpr_expr_ast* arg = producer ? cxpr_expr_ast_producer_arg(ast, i)
                                       : cxpr_expr_ast_call_arg(ast, i);
        args[i] = cxpr_model_inline_locals(arg, locals, local_count);
        if (!args[i]) {
            for (size_t j = 0; j < i; ++j) cxpr_expr_ast_free(args[j]);
            free(args);
            return NULL;
        }
    }
    return args;
}

cxpr_expr_ast* cxpr_model_inline_locals(const cxpr_expr_ast* ast,
                                   const cxpr_model_local_binding* locals,
                                   size_t local_count) {
    cxpr_expr_ast_kind type;
    if (!ast) return NULL;
    type = cxpr_expr_ast_kind_of(ast);

    if (type == CXPR_NODE_IDENTIFIER) {
        const cxpr_expr_ast* replacement =
            cxpr_model_local_lookup(locals, local_count, cxpr_expr_ast_identifier_name(ast));
        if (replacement) return cxpr_expr_ast_clone(replacement);
        return cxpr_expr_ast_clone(ast);
    }

    switch (type) {
    case CXPR_NODE_RECORD: {
        const size_t field_count = cxpr_expr_ast_record_field_count(ast);
        cxpr_expr_ast** values = NULL;
        const char** names = NULL;
        cxpr_model_local_binding* scoped_locals = NULL;
        if (field_count > 0u) {
            values = (cxpr_expr_ast**)calloc(field_count, sizeof(cxpr_expr_ast*));
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
                names[i] = cxpr_expr_ast_record_field_name(ast, i);
                values[i] = cxpr_model_inline_locals(
                    cxpr_expr_ast_record_field_value(ast, i), scoped_locals, local_count + i);
                if (!values[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(values[j]);
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
            cxpr_expr_ast* record = cxpr_expr_ast_record_new(names, values, field_count);
            free(names);
            free(scoped_locals);
            if (!record) {
                for (size_t i = 0u; i < field_count; ++i) cxpr_expr_ast_free(values[i]);
                free(values);
            }
            return record;
        }
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_expr_ast* left = cxpr_model_inline_locals(cxpr_expr_ast_binary_left(ast), locals, local_count);
        cxpr_expr_ast* right = cxpr_model_inline_locals(cxpr_expr_ast_binary_right(ast), locals, local_count);
        if (!left || !right) {
            cxpr_expr_ast_free(left);
            cxpr_expr_ast_free(right);
            return NULL;
        }
        return cxpr_expr_ast_binary_new(cxpr_expr_ast_operator(ast), left, right);
    }
    case CXPR_NODE_UNARY_OP: {
        cxpr_expr_ast* operand = cxpr_model_inline_locals(cxpr_expr_ast_unary_operand(ast), locals, local_count);
        if (!operand) return NULL;
        return cxpr_expr_ast_unary_new(cxpr_expr_ast_operator(ast), operand);
    }
    case CXPR_NODE_FUNCTION_CALL: {
        size_t argc = cxpr_expr_ast_call_arg_count(ast);
        cxpr_expr_ast** args = cxpr_model_inline_args(ast, argc, locals, local_count, false);
        char** arg_names = cxpr_model_clone_arg_names_from_call(ast, argc);
        if (argc > 0u && (!args || (cxpr_expr_ast_call_has_named_args(ast) && !arg_names))) {
            if (args) {
                for (size_t i = 0; i < argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
            }
            return NULL;
        }
        return cxpr_expr_ast_call_named_new(cxpr_expr_ast_call_name(ast), args, arg_names, argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        size_t argc = cxpr_expr_ast_producer_arg_count(ast);
        cxpr_expr_ast** args = cxpr_model_inline_args(ast, argc, locals, local_count, true);
        char** arg_names = cxpr_model_clone_arg_names_from_producer(ast, argc);
        if (argc > 0u && (!args || (cxpr_expr_ast_producer_has_named_args(ast) && !arg_names))) {
            if (args) {
                for (size_t i = 0; i < argc; ++i) cxpr_expr_ast_free(args[i]);
                free(args);
            }
            return NULL;
        }
        return cxpr_expr_ast_producer_field_named_new(cxpr_expr_ast_producer_name(ast), args, arg_names,
                                                  argc, cxpr_expr_ast_producer_field(ast));
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_expr_ast* target = cxpr_model_inline_locals(cxpr_expr_ast_lookback_target(ast), locals, local_count);
        cxpr_expr_ast* index = cxpr_model_inline_locals(cxpr_expr_ast_lookback_index(ast), locals, local_count);
        if (!target || !index) {
            cxpr_expr_ast_free(target);
            cxpr_expr_ast_free(index);
            return NULL;
        }
        return cxpr_expr_ast_lookback_new(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_expr_ast* condition = cxpr_model_inline_locals(cxpr_expr_ast_ternary_condition(ast), locals, local_count);
        cxpr_expr_ast* yes = cxpr_model_inline_locals(cxpr_expr_ast_ternary_true(ast), locals, local_count);
        cxpr_expr_ast* no = cxpr_model_inline_locals(cxpr_expr_ast_ternary_false(ast), locals, local_count);
        if (!condition || !yes || !no) {
            cxpr_expr_ast_free(condition);
            cxpr_expr_ast_free(yes);
            cxpr_expr_ast_free(no);
            return NULL;
        }
        return cxpr_expr_ast_ternary_new(condition, yes, no);
    }
    default:
        return cxpr_expr_ast_clone(ast);
    }
}

static bool cxpr_model_defined_body_is_series_aware(const cxpr_expr_ast* ast,
                                                     const cxpr_registry* registry,
                                                     size_t depth) {
    if (!ast || depth > 64u) return false;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_LOOKBACK:
        return true;
    case CXPR_NODE_FUNCTION_CALL: {
        const char* name = cxpr_expr_ast_call_name(ast);
        cxpr_func_entry* entry;
        if (cxpr_model_window_is_function(name)) return true;
        entry = registry ? cxpr_registry_find(registry, name) : NULL;
        if (entry && entry->defined_body &&
            cxpr_model_defined_body_is_series_aware(
                entry->defined_body, registry, depth + 1u)) {
            return true;
        }
        for (size_t i = 0u; i < cxpr_expr_ast_call_arg_count(ast); ++i) {
            if (cxpr_model_defined_body_is_series_aware(
                    cxpr_expr_ast_call_arg(ast, i), registry, depth)) {
                return true;
            }
        }
        return false;
    }
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < cxpr_expr_ast_record_field_count(ast); ++i) {
            if (cxpr_model_defined_body_is_series_aware(
                    cxpr_expr_ast_record_field_value(ast, i), registry, depth)) {
                return true;
            }
        }
        return false;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_defined_body_is_series_aware(
                   cxpr_expr_ast_binary_left(ast), registry, depth) ||
               cxpr_model_defined_body_is_series_aware(
                   cxpr_expr_ast_binary_right(ast), registry, depth);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_defined_body_is_series_aware(
            cxpr_expr_ast_unary_operand(ast), registry, depth);
    case CXPR_NODE_TERNARY:
        return cxpr_model_defined_body_is_series_aware(
                   cxpr_expr_ast_ternary_condition(ast), registry, depth) ||
               cxpr_model_defined_body_is_series_aware(
                   cxpr_expr_ast_ternary_true(ast), registry, depth) ||
               cxpr_model_defined_body_is_series_aware(
                   cxpr_expr_ast_ternary_false(ast), registry, depth);
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            if (cxpr_model_defined_body_is_series_aware(
                    ast->data.array.elements[i], registry, depth)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

static cxpr_expr_ast* cxpr_model_expand_defined_owned(cxpr_expr_ast* ast,
                                                       const cxpr_registry* registry,
                                                       size_t depth,
                                                       cxpr_error* err) {
    if (!ast || !registry) return ast;
    if (depth > 64u) {
        cxpr_expr_ast_free(ast);
        cxpr_model_set_error(
            err, CXPR_ERR_SYNTAX, "Recursive model function expansion", 0u, 0u);
        return NULL;
    }
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_FUNCTION_CALL: {
        cxpr_func_entry* entry;
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
            ast->data.function_call.args[i] = cxpr_model_expand_defined_owned(
                ast->data.function_call.args[i], registry, depth, err);
            if (!ast->data.function_call.args[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
        entry = cxpr_registry_find(registry, ast->data.function_call.name);
        if (entry && entry->defined_body &&
            cxpr_model_defined_body_is_series_aware(
                entry->defined_body, registry, 0u) &&
            entry->defined_return_field_count == 0u) {
            cxpr_model_local_binding* locals;
            cxpr_expr_ast* substituted;
            if (entry->defined_param_count != ast->data.function_call.argc) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(
                    err, CXPR_ERR_WRONG_ARITY, "Model function arity mismatch", 0u, 0u);
                return NULL;
            }
            locals = (cxpr_model_local_binding*)calloc(
                entry->defined_param_count ? entry->defined_param_count : 1u,
                sizeof(*locals));
            if (!locals) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(
                    err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                return NULL;
            }
            for (size_t i = 0u; i < entry->defined_param_count; ++i) {
                locals[i].name = entry->defined_param_names[i];
                locals[i].expr = ast->data.function_call.args[i];
            }
            substituted = cxpr_model_inline_locals(
                entry->defined_body, locals, entry->defined_param_count);
            free(locals);
            cxpr_expr_ast_free(ast);
            if (!substituted) {
                cxpr_model_set_error(
                    err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                return NULL;
            }
            return cxpr_model_expand_defined_owned(
                substituted, registry, depth + 1u, err);
        }
        return ast;
    }
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            ast->data.record.field_values[i] = cxpr_model_expand_defined_owned(
                ast->data.record.field_values[i], registry, depth, err);
            if (!ast->data.record.field_values[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
        return ast;
    case CXPR_NODE_BINARY_OP:
        ast->data.binary_op.left = cxpr_model_expand_defined_owned(
            ast->data.binary_op.left, registry, depth, err);
        ast->data.binary_op.right = cxpr_model_expand_defined_owned(
            ast->data.binary_op.right, registry, depth, err);
        break;
    case CXPR_NODE_UNARY_OP:
        ast->data.unary_op.operand = cxpr_model_expand_defined_owned(
            ast->data.unary_op.operand, registry, depth, err);
        break;
    case CXPR_NODE_LOOKBACK:
        ast->data.lookback.target = cxpr_model_expand_defined_owned(
            ast->data.lookback.target, registry, depth, err);
        ast->data.lookback.index = cxpr_model_expand_defined_owned(
            ast->data.lookback.index, registry, depth, err);
        break;
    case CXPR_NODE_TERNARY:
        ast->data.ternary.condition = cxpr_model_expand_defined_owned(
            ast->data.ternary.condition, registry, depth, err);
        ast->data.ternary.true_branch = cxpr_model_expand_defined_owned(
            ast->data.ternary.true_branch, registry, depth, err);
        ast->data.ternary.false_branch = cxpr_model_expand_defined_owned(
            ast->data.ternary.false_branch, registry, depth, err);
        break;
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            ast->data.array.elements[i] = cxpr_model_expand_defined_owned(
                ast->data.array.elements[i], registry, depth, err);
            if (!ast->data.array.elements[i]) {
                cxpr_expr_ast_free(ast);
                return NULL;
            }
        }
        return ast;
    default:
        return ast;
    }
    if ((cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP &&
         (!ast->data.binary_op.left || !ast->data.binary_op.right)) ||
        (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_UNARY_OP &&
         !ast->data.unary_op.operand) ||
        (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_LOOKBACK &&
         (!ast->data.lookback.target || !ast->data.lookback.index)) ||
        (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_TERNARY &&
         (!ast->data.ternary.condition || !ast->data.ternary.true_branch ||
          !ast->data.ternary.false_branch))) {
        cxpr_expr_ast_free(ast);
        return NULL;
    }
    return ast;
}

cxpr_expr_ast* cxpr_model_inline_defined_calls(const cxpr_expr_ast* ast,
                                               const cxpr_registry* registry,
                                               cxpr_error* err) {
    cxpr_expr_ast* clone;
    if (!ast) return NULL;
    clone = cxpr_expr_ast_clone(ast);
    if (!clone || !registry) return clone;
    return cxpr_model_expand_defined_owned(clone, registry, 0u, err);
}
