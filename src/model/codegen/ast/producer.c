#include "model/codegen/ast/internal.h"
#include "model/window/plan.h"
#include "model/window/window.h"
#include "registry/internal.h"
#include <cxpr/codegen.h>
#include <cxpr/resample.h>
#include "eval/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* cxpr_model_ast_field_expr_to_c(const cxpr_model_compiled* program,
                                            const cxpr_expr_ast* ast,
                                            const char* field,
                                            const cxpr_c_target* target,
                                            cxpr_error* err,
                                            unsigned depth);

char* cxpr_model_ast_producer_access_to_c(const cxpr_model_compiled* program,
                                                 const cxpr_expr_ast* ast,
                                                 const char* function_prefix,
                                                 const double* literal_param_values,
                                                 size_t literal_param_count,
                                                 char** child_call_keys,
                                                 size_t* child_call_child_indices,
                                                 size_t child_call_count,
                                                 cxpr_error* err) {
    cxpr_func_entry* entry;
    const char* producer;
    const char* field;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target target_data;
    cxpr_c_target target;

    if (!program || !program->registry || !ast ||
        cxpr_expr_ast_kind_of(ast) != CXPR_NODE_PRODUCER_ACCESS) {
        return NULL;
    }
    producer = cxpr_expr_ast_producer_name(ast);
    field = cxpr_expr_ast_producer_field(ast);
    entry = cxpr_registry_find(program->registry, producer);
    if (!entry || (!entry->model_producer && entry->defined_return_field_count == 0u &&
                   !entry->struct_codegen) ||
        (!entry->model_producer && !entry->struct_codegen &&
         entry->defined_param_count != cxpr_expr_ast_producer_arg_count(ast)) ||
        (entry->model_producer &&
         (cxpr_expr_ast_producer_arg_count(ast) < entry->min_args ||
          cxpr_expr_ast_producer_arg_count(ast) >
              entry->max_args +
                  (((const cxpr_model_child_program*)entry->model_producer_userdata)->program
                       ? ((const cxpr_model_child_program*)entry->model_producer_userdata)->program->input_count
                       : 0u)))) {
        if (err) {
            static CXPR_THREAD_LOCAL char message[256];
            const cxpr_model_child_program* child_ref =
                entry && entry->model_producer_userdata
                    ? (const cxpr_model_child_program*)entry->model_producer_userdata
                    : NULL;
            err->code = CXPR_ERR_UNKNOWN_FUNCTION;
            snprintf(message, sizeof(message),
                     "Unsupported model C producer access '%s.%s' (child=%s argc=%lu min=%lu max=%lu)",
                     producer ? producer : "?", field ? field : "?",
                     child_ref && child_ref->name ? child_ref->name : "none",
                     (unsigned long)cxpr_expr_ast_producer_arg_count(ast),
                     (unsigned long)(entry ? entry->min_args : 0u),
                     (unsigned long)(entry ? entry->max_args : 0u));
            err->message = message;
        }
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            field &&
            strcmp(entry->defined_return_field_names[i], field) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1 && entry->struct_codegen) {
        for (size_t i = 0u; i < entry->fields_per_arg; ++i) {
            if (entry->struct_fields[i] && field &&
                cxpr_model_names_match(entry->struct_fields[i], field)) {
                selected_field = i;
                break;
            }
        }
    }
    if (selected_field == (size_t)-1 && field && producer &&
        cxpr_model_names_match(producer, "swing_pivots")) {
        const char* canonical = cxpr_model_names_match(field, "high")
            ? "pivot_high"
            : (cxpr_model_names_match(field, "low")
                   ? "pivot_low"
                   : (cxpr_model_names_match(field, "line") ? "swing_line" : NULL));
        for (size_t i = 0u; canonical && i < entry->defined_return_field_count; ++i) {
            if (entry->defined_return_field_names[i] &&
                cxpr_model_names_match(entry->defined_return_field_names[i], canonical)) {
                selected_field = i;
                break;
            }
        }
    }
    if (selected_field == (size_t)-1) {
        if (err) {
            static CXPR_THREAD_LOCAL char message[256];
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            snprintf(message, sizeof(message), "Unknown producer field '%s.%s'",
                     producer ? producer : "?", field ? field : "?");
            err->message = message;
        }
        return NULL;
    }

    if (entry->struct_codegen) {
        const size_t argc = cxpr_expr_ast_producer_arg_count(ast);
        char* expression;
        arg_exprs = (char**)calloc(argc ? argc : 1u, sizeof(char*));
        if (!arg_exprs) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < argc; ++i) {
            arg_exprs[i] = cxpr_expr_ast_to_c_at_offset(
                cxpr_expr_ast_producer_arg(ast, i), 0u, &target, err);
            if (!arg_exprs[i]) {
                for (size_t j = 0u; j < i; ++j) free(arg_exprs[j]);
                free(arg_exprs);
                return NULL;
            }
        }
        expression = entry->struct_codegen(
            field, (const char* const*)arg_exprs, argc,
            entry->struct_codegen_userdata, err);
        for (size_t i = 0u; i < argc; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return expression;
    }

    if (entry->model_producer) {
        size_t child_index = cxpr_model_c_child_index_for_entry(program, entry);
        const cxpr_model_compiled* child =
            child_index == (size_t)-1 ? NULL : program->children[child_index].program;
        size_t child_call_index;
        char* child_call_key;
        char* helper_name;
        cxpr_model_c_buf call = {0};
        if (!child) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model producer", 0, 0);
            return NULL;
        }
        child_call_key = cxpr_model_c_child_call_key(ast);
        if (!child_call_key) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        child_call_index = cxpr_model_c_child_call_index_for_key(
            child_call_keys,
            child_call_child_indices,
            child_call_count,
            child_index,
            child_call_key);
        free(child_call_key);
        if (child_call_index == (size_t)-1) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unknown child model callsite", 0, 0);
            return NULL;
        }
        (void)producer;
        helper_name = cxpr_model_c_child_field_name(function_prefix, child_index, selected_field);
        if (!helper_name) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        cxpr_model_c_printf(&call,
                            "%s(&_cx_state->child_call_%zu_initialized, _cx_state->child_call_%zu_outputs, &_cx_state->child_call_%zu_state",
                            helper_name,
                            child_call_index,
                            child_call_index,
                            child_call_index);
        free(helper_name);
        for (size_t i = 0u; i < child->input_count; ++i) {
            size_t input_index = 0u;
            const cxpr_expr_ast* source_arg =
                (i == program->children[child_index].source_input_index)
                    ? cxpr_model_child_call_source_arg(&program->children[child_index], child, ast)
                    : NULL;
            if (!source_arg &&
                i == program->children[child_index].source_input_index &&
                i < cxpr_expr_ast_producer_arg_count(ast)) {
                source_arg = cxpr_expr_ast_producer_arg(ast, i);
            }
            char* source_expr = NULL;
            target_data = (cxpr_model_ast_c_target){
                .program = program,
                .function_prefix = function_prefix,
                .literal_param_values = literal_param_values,
                .literal_param_count = literal_param_count,
                .child_call_keys = child_call_keys,
                .child_call_child_indices = child_call_child_indices,
                .child_call_count = child_call_count,
            };
            target = (cxpr_c_target){
                .api_version = CXPR_C_TARGET_API_VERSION,
                .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
                .emit_call_at_offset = cxpr_model_ast_c_emit_call,
                .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
                .userdata = &target_data,
            };
            if (source_arg) {
                source_expr = cxpr_expr_ast_to_c_at_offset(source_arg, 0u, &target, err);
                if (!source_expr) {
                    free(call.data);
                    return NULL;
                }
                cxpr_model_c_printf(&call, ", %s", source_expr);
                free(source_expr);
                continue;
            }
            if (!cxpr_model_c_symbol_is_input(program, child->inputs[i], &input_index)) {
                free(call.data);
                {
                    static CXPR_THREAD_LOCAL char message[256];
                    snprintf(message, sizeof(message),
                             "Child model input '%s' is not a parent input",
                             child->inputs[i] ? child->inputs[i] : "");
                    cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                         message, 0, 0);
                }
                return NULL;
            }
            cxpr_model_c_printf(&call, ", _cx_input_%zu", input_index);
        }
        target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
        target = (cxpr_c_target){
            .api_version = CXPR_C_TARGET_API_VERSION,
            .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
            .emit_call_at_offset = cxpr_model_ast_c_emit_call,
            .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
            .userdata = &target_data,
        };
        for (size_t i = 0u; i < child->constant_count; ++i) {
            const cxpr_expr_ast* arg = cxpr_model_child_call_param_arg(
                &program->children[child_index], child, ast, i);
            char* arg_expr;
            if (!arg && child->constants[i].ast) {
                arg = child->constants[i].ast;
            }
            if (!arg) {
                free(call.data);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Missing producer argument", 0, 0);
                return NULL;
            }
            arg_expr = cxpr_expr_ast_to_c_at_offset(arg, 0u, &target, err);
            if (!arg_expr) {
                free(call.data);
                return NULL;
            }
            cxpr_model_c_printf(&call, ", %s", arg_expr);
            free(arg_expr);
        }
        cxpr_model_c_puts(&call, ")");
        if (call.oom) {
            free(call.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return call.data;
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                              sizeof(char*));
    if (!arg_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }
    target_data = (cxpr_model_ast_c_target){
            .program = program,
            .function_prefix = function_prefix,
            .literal_param_values = literal_param_values,
            .literal_param_count = literal_param_count,
            .child_call_keys = child_call_keys,
            .child_call_child_indices = child_call_child_indices,
            .child_call_count = child_call_count,
        };
    target = (cxpr_c_target){
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = cxpr_model_ast_c_emit_leaf,
        .emit_call_at_offset = cxpr_model_ast_c_emit_call,
        .emit_lookback_at_offset = cxpr_model_ast_c_emit_lookback,
        .userdata = &target_data,
    };
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        const cxpr_expr_ast* arg = cxpr_model_producer_arg_for_param(
            ast, entry->defined_param_names ? entry->defined_param_names[i] : NULL, i);
        if (!arg) {
            if (err) {
                err->code = CXPR_ERR_SYNTAX;
                err->message = "Missing producer argument";
            }
            goto fail;
        }
        arg_exprs[i] = cxpr_expr_ast_to_c_at_offset(arg, 0u, &target, err);
        if (!arg_exprs[i]) goto fail;
    }

    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        goto fail;
    }
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        target_data.param_names = names;
        target_data.param_exprs = exprs;
        target_data.param_count = name_count;
        field_exprs[field_i] = cxpr_expr_ast_to_c_at_offset(
            entry->defined_return_field_bodies[field_i], 0u, &target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }
    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    free(arg_exprs);
    return NULL;
}

static const char* cxpr_model_c_ast_binary_op(int op) {
    switch (op) {
    case CXPR_TOK_PLUS: return "+";
    case CXPR_TOK_MINUS: return "-";
    case CXPR_TOK_STAR: return "*";
    case CXPR_TOK_SLASH: return "/";
    case CXPR_TOK_EQ: return "==";
    case CXPR_TOK_NEQ: return "!=";
    case CXPR_TOK_LT: return "<";
    case CXPR_TOK_GT: return ">";
    case CXPR_TOK_LTE: return "<=";
    case CXPR_TOK_GTE: return ">=";
    case CXPR_TOK_AND: return "&&";
    case CXPR_TOK_OR: return "||";
    default: return NULL;
    }
}

bool cxpr_model_ast_is_record_like(const cxpr_model_compiled* program,
                                          const cxpr_expr_ast* ast,
                                          unsigned depth) {
    cxpr_func_entry* entry;
    const cxpr_model_compiled_binding* binding;

    if (!program || !ast || depth > 32u) return false;
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_RECORD:
        return true;
    case CXPR_NODE_IDENTIFIER:
        binding = cxpr_model_c_binding_for_name(program, cxpr_expr_ast_identifier_name(ast));
        return binding ? cxpr_model_ast_is_record_like(program, binding->ast, depth + 1u) : false;
    case CXPR_NODE_FUNCTION_CALL:
        entry = program->registry
                    ? cxpr_registry_find(program->registry, cxpr_expr_ast_call_name(ast))
                    : NULL;
        return entry && entry->defined_return_field_count > 1u;
    default:
        return false;
    }
}

static char* cxpr_model_ast_field_expr_try(const cxpr_model_compiled* program,
                                           const cxpr_expr_ast* ast,
                                           const char* field,
                                           const cxpr_c_target* target,
                                           unsigned depth) {
    cxpr_error ignored = {0};
    return cxpr_model_ast_field_expr_to_c(program, ast, field, target, &ignored, depth);
}

static char* cxpr_model_ast_record_function_field_to_c(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    const char* field,
    const cxpr_c_target* target,
    cxpr_error* err,
    unsigned depth) {
    cxpr_func_entry* entry;
    size_t selected_field = (size_t)-1;
    char** arg_exprs = NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target* target_data = target ? (cxpr_model_ast_c_target*)target->userdata : NULL;
    cxpr_model_ast_c_target inline_data;
    cxpr_c_target inline_target;

    if (!program || !program->registry || !ast ||
        cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL || !field || depth > 32u) {
        return NULL;
    }
    entry = cxpr_registry_find(program->registry, cxpr_expr_ast_call_name(ast));
    if (!entry || entry->defined_return_field_count == 0u ||
        (!entry->model_producer &&
         entry->defined_param_count != cxpr_expr_ast_call_arg_count(ast))) {
        return NULL;
    }
    for (size_t i = 0u; i < entry->defined_return_field_count; ++i) {
        if (entry->defined_return_field_names[i] &&
            cxpr_model_names_match(entry->defined_return_field_names[i], field)) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) return NULL;

    if (entry->model_producer) {
        cxpr_expr_ast producer = {0};
        producer.type = CXPR_NODE_PRODUCER_ACCESS;
        producer.data.producer_access.name = ast->data.function_call.name;
        producer.data.producer_access.args = ast->data.function_call.args;
        producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
        producer.data.producer_access.argc = ast->data.function_call.argc;
        producer.data.producer_access.field = (char*)field;
        return cxpr_model_ast_producer_access_to_c(
            program,
            &producer,
            target_data ? target_data->function_prefix : NULL,
            target_data ? target_data->literal_param_values : NULL,
            target_data ? target_data->literal_param_count : 0u,
            target_data ? target_data->child_call_keys : NULL,
            target_data ? target_data->child_call_child_indices : NULL,
            target_data ? target_data->child_call_count : 0u,
            err);
    }

    arg_exprs = (char**)calloc(entry->defined_param_count ? entry->defined_param_count : 1u,
                               sizeof(char*));
    field_exprs = (char**)calloc(entry->defined_return_field_count, sizeof(char*));
    if (!arg_exprs || !field_exprs) {
        free(arg_exprs);
        free(field_exprs);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        arg_exprs[i] = cxpr_expr_ast_to_c(cxpr_expr_ast_call_arg(ast, i), target, err);
        if (!arg_exprs[i]) goto fail;
    }

    inline_data = target_data ? *target_data : (cxpr_model_ast_c_target){0};
    inline_target = *target;
    inline_target.userdata = &inline_data;
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t name_count = entry->defined_param_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < entry->defined_param_count; ++i) {
            names[i] = entry->defined_param_names[i];
            exprs[i] = arg_exprs[i];
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[entry->defined_param_count + i] = entry->defined_return_field_names[i];
            exprs[entry->defined_param_count + i] = field_exprs[i];
        }
        inline_data.param_names = names;
        inline_data.param_exprs = exprs;
        inline_data.param_count = name_count;
        field_exprs[field_i] = cxpr_expr_ast_to_c(
            entry->defined_return_field_bodies[field_i], &inline_target, err);
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }

    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
        free(field_exprs);
        free(arg_exprs);
        return out;
    }

fail:
    if (field_exprs) {
        for (size_t i = 0u; i < entry->defined_return_field_count; ++i) free(field_exprs[i]);
    }
    if (arg_exprs) {
        for (size_t i = 0u; i < entry->defined_param_count; ++i) free(arg_exprs[i]);
    }
    free(field_exprs);
    free(arg_exprs);
    return NULL;
}

static char* cxpr_model_ast_record_field_to_c(
    const cxpr_model_compiled* program,
    const cxpr_expr_ast* ast,
    const char* field,
    const cxpr_c_target* target,
    cxpr_error* err,
    unsigned depth) {
    size_t selected_field = (size_t)-1;
    const size_t field_count = cxpr_expr_ast_record_field_count(ast);
    const char* dot = strchr(field, '.');
    const size_t segment_len = dot ? (size_t)(dot - field) : strlen(field);
    const char* rest = dot ? dot + 1 : NULL;
    char** field_exprs = NULL;
    cxpr_model_ast_c_target* target_data = target ? (cxpr_model_ast_c_target*)target->userdata : NULL;
    cxpr_model_ast_c_target inline_data;
    cxpr_c_target inline_target;

    if (!program || !ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_RECORD ||
        !field || !target || depth > 32u) {
        return NULL;
    }
    if (segment_len == 0u || (rest && rest[0] == '\0')) return NULL;
    for (size_t i = 0u; i < field_count; ++i) {
        const char* field_name = cxpr_expr_ast_record_field_name(ast, i);
        if (field_name &&
            strlen(field_name) == segment_len &&
            strncmp(field_name, field, segment_len) == 0) {
            selected_field = i;
            break;
        }
    }
    if (selected_field == (size_t)-1) return NULL;

    field_exprs = (char**)calloc(field_count ? field_count : 1u, sizeof(char*));
    if (!field_exprs) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    inline_data = target_data ? *target_data : (cxpr_model_ast_c_target){0};
    inline_target = *target;
    inline_target.userdata = &inline_data;
    for (size_t field_i = 0u; field_i <= selected_field; ++field_i) {
        const size_t base_count = target_data ? target_data->param_count : 0u;
        const size_t name_count = base_count + field_i;
        char** names = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        char** exprs = (char**)calloc(name_count ? name_count : 1u, sizeof(char*));
        if (!names || !exprs) {
            free(names);
            free(exprs);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            goto fail;
        }
        for (size_t i = 0u; i < base_count; ++i) {
            names[i] = target_data->param_names[i];
            exprs[i] = target_data->param_exprs ? target_data->param_exprs[i] : NULL;
        }
        for (size_t i = 0u; i < field_i; ++i) {
            names[base_count + i] = (char*)cxpr_expr_ast_record_field_name(ast, i);
            exprs[base_count + i] = field_exprs[i];
        }
        inline_data.param_names = names;
        inline_data.param_exprs = exprs;
        inline_data.param_count = name_count;
        if (field_i == selected_field && rest) {
            field_exprs[field_i] = cxpr_model_ast_field_expr_to_c(
                program,
                cxpr_expr_ast_record_field_value(ast, field_i),
                rest,
                &inline_target,
                err,
                depth + 1u);
        } else {
            field_exprs[field_i] = cxpr_expr_ast_to_c(
                cxpr_expr_ast_record_field_value(ast, field_i), &inline_target, err);
        }
        free(names);
        free(exprs);
        if (!field_exprs[field_i]) goto fail;
    }

    {
        char* out = field_exprs[selected_field];
        field_exprs[selected_field] = NULL;
        for (size_t i = 0u; i < field_count; ++i) free(field_exprs[i]);
        free(field_exprs);
        return out;
    }

fail:
    for (size_t i = 0u; i < field_count; ++i) free(field_exprs[i]);
    free(field_exprs);
    return NULL;
}

char* cxpr_model_ast_field_expr_to_c(const cxpr_model_compiled* program,
                                            const cxpr_expr_ast* ast,
                                            const char* field,
                                            const cxpr_c_target* target,
                                            cxpr_error* err,
                                            unsigned depth) {
    if (!program || !ast || !field || !target || depth > 32u) return NULL;

    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_RECORD:
        return cxpr_model_ast_record_field_to_c(program, ast, field, target, err, depth + 1u);
    case CXPR_NODE_IDENTIFIER: {
        const cxpr_model_compiled_binding* binding =
            cxpr_model_c_binding_for_name(program, cxpr_expr_ast_identifier_name(ast));
        return binding
                   ? cxpr_model_ast_field_expr_to_c(
                         program, binding->ast, field, target, err, depth + 1u)
                   : NULL;
    }
    case CXPR_NODE_VARIABLE: {
        const char* variable_name = cxpr_expr_ast_param_name(ast);
        const char* dot = variable_name ? strchr(variable_name, '.') : NULL;
        if (dot && dot > variable_name && dot[1] != '\0') {
            size_t root_len = (size_t)(dot - variable_name);
            char* root = (char*)malloc(root_len + 1u);
            if (!root) return NULL;
            memcpy(root, variable_name, root_len);
            root[root_len] = '\0';
            const cxpr_model_compiled_binding* dotted_constant =
                cxpr_model_c_constant_for_name(program, root);
            free(root);
            if (dotted_constant) {
                return cxpr_model_ast_field_expr_to_c(
                    program, dotted_constant->ast, dot + 1, target, err, depth + 1u);
            }
        }
        const cxpr_model_compiled_binding* constant =
            cxpr_model_c_constant_for_name(program, variable_name);
        return constant
                   ? cxpr_model_ast_field_expr_to_c(
                         program, constant->ast, field, target, err, depth + 1u)
                   : NULL;
    }
    case CXPR_NODE_FUNCTION_CALL: {
        char* expression = cxpr_model_ast_record_function_field_to_c(
            program, ast, field, target, err, depth + 1u);
        cxpr_model_ast_c_target* target_data;
        cxpr_expr_ast producer = {0};
        if (expression) return expression;
        target_data = (cxpr_model_ast_c_target*)target->userdata;
        producer.type = CXPR_NODE_PRODUCER_ACCESS;
        producer.data.producer_access.name = ast->data.function_call.name;
        producer.data.producer_access.args = ast->data.function_call.args;
        producer.data.producer_access.arg_names = ast->data.function_call.arg_names;
        producer.data.producer_access.argc = ast->data.function_call.argc;
        producer.data.producer_access.field = (char*)field;
        return cxpr_model_ast_producer_access_to_c(
            program, &producer,
            target_data ? target_data->function_prefix : NULL,
            target_data ? target_data->literal_param_values : NULL,
            target_data ? target_data->literal_param_count : 0u,
            target_data ? target_data->child_call_keys : NULL,
            target_data ? target_data->child_call_child_indices : NULL,
            target_data ? target_data->child_call_count : 0u,
            err);
    }
    case CXPR_NODE_BINARY_OP: {
        const char* op = cxpr_model_c_ast_binary_op(cxpr_expr_ast_operator(ast));
        char* left_field;
        char* right_field;
        char* left;
        char* right;
        cxpr_model_c_buf b = {0};
        if (!op) return NULL;
        left_field = cxpr_model_ast_field_expr_try(
            program, cxpr_expr_ast_binary_left(ast), field, target, depth + 1u);
        right_field = cxpr_model_ast_field_expr_try(
            program, cxpr_expr_ast_binary_right(ast), field, target, depth + 1u);
        if (left_field) {
            left = left_field;
        } else {
            left = cxpr_expr_ast_to_c(cxpr_expr_ast_binary_left(ast), target, err);
        }
        if (right_field) {
            right = right_field;
        } else {
            right = cxpr_expr_ast_to_c(cxpr_expr_ast_binary_right(ast), target, err);
        }
        if (!left || !right) {
            free(left);
            free(right);
            return NULL;
        }
        cxpr_model_c_printf(&b, "(%s %s %s)", left, op, right);
        free(left);
        free(right);
        if (b.oom) {
            free(b.data);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return NULL;
        }
        return b.data;
    }
    default:
        return NULL;
    }
}
