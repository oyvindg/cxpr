#include "model/internal.h"
#include "model/window/window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        cxpr_expr_ast* out = cxpr_expr_ast_call_named_new(cxpr_expr_ast_call_name(ast), args, arg_names, argc);
        if (out && cxpr_expr_ast_call_instance_id(ast) &&
            !cxpr_expr_ast_call_set_instance_id(out, cxpr_expr_ast_call_instance_id(ast))) {
            cxpr_expr_ast_free(out); return NULL;
        }
        return out;
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
        cxpr_expr_ast* out = cxpr_expr_ast_producer_field_named_new(cxpr_expr_ast_producer_name(ast), args, arg_names,
                                                  argc, cxpr_expr_ast_producer_field(ast));
        if (out && cxpr_expr_ast_call_instance_id(ast) &&
            !cxpr_expr_ast_call_set_instance_id(out, cxpr_expr_ast_call_instance_id(ast))) {
            cxpr_expr_ast_free(out); return NULL;
        }
        return out;
    }
    case CXPR_NODE_FIELD_ACCESS: {
        const cxpr_expr_ast* base = cxpr_expr_ast_field_base(ast);
        if (!base) {
            const cxpr_expr_ast* replacement = cxpr_model_local_lookup(
                locals, local_count, cxpr_expr_ast_field_object(ast));
            if (replacement && replacement->type == CXPR_NODE_RECORD) {
                const char* field = cxpr_expr_ast_field_name(ast);
                for (size_t i = 0u; i < replacement->data.record.field_count; ++i) {
                    if (strcmp(replacement->data.record.field_names[i], field) == 0)
                        return cxpr_expr_ast_clone(
                            replacement->data.record.field_values[i]);
                }
            }
            return cxpr_expr_ast_clone(ast);
        }
        {
            cxpr_expr_ast* expanded = cxpr_model_inline_locals(
                base, locals, local_count);
            if (!expanded) return NULL;
            return cxpr_expr_ast_field_expr_new(
                expanded, cxpr_expr_ast_field_name(ast));
        }
    }
    case CXPR_NODE_INDEX: {
        cxpr_expr_ast* target = cxpr_model_inline_locals(cxpr_expr_ast_index_target(ast), locals, local_count);
        cxpr_expr_ast* index = cxpr_model_inline_locals(cxpr_expr_ast_index_expression(ast), locals, local_count);
        if (!target || !index) {
            cxpr_expr_ast_free(target);
            cxpr_expr_ast_free(index);
            return NULL;
        }
        return cxpr_expr_ast_index_new(target, index);
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
    case CXPR_NODE_INDEX:
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
    if (!ast) return ast;
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
        if (strcmp(ast->data.function_call.name, "map") == 0 &&
            ast->data.function_call.argc == 2u &&
            (ast->data.function_call.args[0]->type == CXPR_NODE_RECORD ||
             ((ast->data.function_call.args[0]->type == CXPR_NODE_ARRAY) &&
              ast->data.function_call.args[1]->type == CXPR_NODE_FUNCTION_CALL &&
              ast->data.function_call.args[1]->data.function_call.argc == 2u &&
              (ast->data.function_call.args[1]->data.function_call.args[1]->type ==
                   CXPR_NODE_FUNCTION_CALL ||
               ast->data.function_call.args[1]->data.function_call.args[1]->type ==
                   CXPR_NODE_PRODUCER_ACCESS)))) {
            const cxpr_expr_ast* source;
            const cxpr_expr_ast* lambda;
            const cxpr_expr_ast* parameter;
            const cxpr_expr_ast* body;
            const char* implicit_output = NULL;
            cxpr_expr_ast** elements;
            const char** field_names = NULL;
            bool record_source;
            size_t count;
            if (ast->data.function_call.argc != 2u) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_WRONG_ARITY,
                                     "map expects a static collection and callback", 0u, 0u);
                return NULL;
            }
            source = ast->data.function_call.args[0];
            lambda = ast->data.function_call.args[1];
            record_source = source && source->type == CXPR_NODE_RECORD;
            if (!source || (source->type != CXPR_NODE_ARRAY && !record_source)) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "map source must be a static array or record", 0u, 0u);
                return NULL;
            }
            if (!lambda || lambda->type != CXPR_NODE_FUNCTION_CALL ||
                strcmp(lambda->data.function_call.name, "__cxpr_lambda") != 0 ||
                lambda->data.function_call.argc != 2u ||
                lambda->data.function_call.args[0]->type != CXPR_NODE_IDENTIFIER) {
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "map expects a single-parameter callback", 0u, 0u);
                return NULL;
            }
            parameter = lambda->data.function_call.args[0];
            body = lambda->data.function_call.args[1];
            if (body->type == CXPR_NODE_FUNCTION_CALL) {
                cxpr_func_entry* mapped_entry = registry
                    ? cxpr_registry_find(registry, body->data.function_call.name)
                    : NULL;
                cxpr_model_child_program* mapped_child =
                    mapped_entry && mapped_entry->model_producer
                        ? (cxpr_model_child_program*)mapped_entry->model_producer_userdata
                        : NULL;
                if (mapped_child && mapped_child->program) {
                    if (mapped_child->program->output_count != 1u) {
                        cxpr_expr_ast_free(ast);
                        cxpr_model_set_error(
                            err, CXPR_ERR_SYNTAX,
                            "map callback must select an output from a multi-output model",
                            0u, 0u);
                        return NULL;
                    }
                    implicit_output = mapped_child->program->outputs[0];
                }
            }
            count = record_source ? source->data.record.field_count : source->data.array.count;
            elements = (cxpr_expr_ast**)calloc(count ? count : 1u, sizeof(*elements));
            if (record_source)
                field_names = (const char**)calloc(count ? count : 1u, sizeof(*field_names));
            if (!elements || (record_source && !field_names)) {
                free(elements);
                free(field_names);
                cxpr_expr_ast_free(ast);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                return NULL;
            }
            for (size_t i = 0u; i < count; ++i) {
                cxpr_model_local_binding local = {
                    .name = parameter->data.identifier.name,
                    .expr = record_source ? source->data.record.field_values[i]
                                          : source->data.array.elements[i]
                };
                char derived_identity[64];
                if (record_source) {
                    field_names[i] = source->data.record.field_names[i];
                    snprintf(derived_identity, sizeof(derived_identity), "%s", field_names[i]);
                } else {
                    snprintf(derived_identity, sizeof(derived_identity), "%zu", i);
                }
                elements[i] = cxpr_model_inline_locals(body, &local, 1u);
                if (elements[i] && implicit_output &&
                    elements[i]->type == CXPR_NODE_FUNCTION_CALL) {
                    cxpr_expr_ast* call = elements[i];
                    elements[i] = cxpr_expr_ast_producer_field_named_new(
                        call->data.function_call.name,
                        call->data.function_call.args,
                        call->data.function_call.arg_names,
                        call->data.function_call.argc,
                        implicit_output);
                    call->data.function_call.args = NULL;
                    call->data.function_call.arg_names = NULL;
                    call->data.function_call.argc = 0u;
                    cxpr_expr_ast_free(call);
                }
                if (!elements[i] ||
                    ((elements[i]->type == CXPR_NODE_FUNCTION_CALL ||
                      elements[i]->type == CXPR_NODE_PRODUCER_ACCESS) &&
                     !cxpr_expr_ast_call_set_instance_id(
                         elements[i], derived_identity))) {
                    for (size_t j = 0u; j <= i; ++j) cxpr_expr_ast_free(elements[j]);
                    free(elements);
                    free(field_names);
                    cxpr_expr_ast_free(ast);
                    cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                    return NULL;
                }
            }
            {
                cxpr_expr_ast* expanded = record_source
                    ? cxpr_expr_ast_record_new(field_names, elements, count)
                    : cxpr_expr_ast_array_new(elements, count);
                cxpr_expr_ast_free(ast);
                ast = expanded;
            }
            free(field_names);
            if (!ast) {
                for (size_t i = 0u; i < count; ++i) cxpr_expr_ast_free(elements[i]);
                free(elements);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                return NULL;
            }
            return ast;
        }
        entry = registry
            ? cxpr_registry_find(registry, ast->data.function_call.name)
            : NULL;
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
    case CXPR_NODE_INDEX:
        ast->data.index.target = cxpr_model_expand_defined_owned(
            ast->data.index.target, registry, depth, err);
        ast->data.index.index = cxpr_model_expand_defined_owned(
            ast->data.index.index, registry, depth, err);
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
        (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_INDEX &&
         (!ast->data.index.target || !ast->data.index.index)) ||
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
    if (!clone) return NULL;
    return cxpr_model_expand_defined_owned(clone, registry, 0u, err);
}

static bool cxpr_model_static_map_expr_is_registry_independent(
    const cxpr_expr_ast* ast) {
    if (!ast) return false;
    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_INT64:
    case CXPR_NODE_BOOL:
    case CXPR_NODE_STRING:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
    case CXPR_NODE_CHAIN_ACCESS:
        return true;
    case CXPR_NODE_FIELD_ACCESS:
        return !ast->data.field_access.base ||
               cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.field_access.base);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_static_map_expr_is_registry_independent(
            ast->data.unary_op.operand);
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.binary_op.left) &&
               cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.binary_op.right);
    case CXPR_NODE_TERNARY:
        return cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.ternary.condition) &&
               cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.ternary.true_branch) &&
               cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.ternary.false_branch);
    case CXPR_NODE_INDEX:
        return cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.index.target) &&
               cxpr_model_static_map_expr_is_registry_independent(
                   ast->data.index.index);
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i)
            if (!cxpr_model_static_map_expr_is_registry_independent(
                    ast->data.array.elements[i])) return false;
        return true;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i)
            if (!cxpr_model_static_map_expr_is_registry_independent(
                    ast->data.record.field_values[i])) return false;
        return true;
    default:
        return false;
    }
}

cxpr_expr_ast* cxpr_model_prelower_static_map(const cxpr_expr_ast* ast) {
    cxpr_error ignored = {0};
    const cxpr_expr_ast* source;
    const cxpr_expr_ast* lambda;
    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL ||
        strcmp(ast->data.function_call.name, "map") != 0 ||
        ast->data.function_call.argc != 2u) return NULL;
    source = ast->data.function_call.args[0];
    lambda = ast->data.function_call.args[1];
    if (!source || (source->type != CXPR_NODE_ARRAY &&
                    source->type != CXPR_NODE_RECORD) ||
        !lambda || lambda->type != CXPR_NODE_FUNCTION_CALL ||
        strcmp(lambda->data.function_call.name, "__cxpr_lambda") != 0 ||
        lambda->data.function_call.argc != 2u ||
        !cxpr_model_static_map_expr_is_registry_independent(
            lambda->data.function_call.args[1])) return NULL;
    return cxpr_model_inline_defined_calls(ast, NULL, &ignored);
}
