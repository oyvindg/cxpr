/**
 * @file eval/calls/producer.c
 * @brief Cached producer-field evaluation.
 */

#include "eval/internal.h"
#include "core.h"
#include "ir/internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* cxpr_eval_producer_unknown_function_message(const char* name) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

cxpr_value cxpr_eval_cached_producer_access(const cxpr_ast* ast,
                                            const cxpr_context* ctx,
                                            const cxpr_registry* reg,
                                            cxpr_error* err) {
    cxpr_ast* mutable_ast = (cxpr_ast*)ast;
    cxpr_func_entry* entry = cxpr_eval_cached_producer_entry(ast, reg);
    const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
    const cxpr_struct_value* produced;
    char const_key_local[256];
    char* const_key_heap = NULL;
    const char* const_key;
    bool found = false;

    if (!entry || (!entry->struct_producer && !entry->model_producer &&
                   entry->defined_return_field_count == 0u)) {
        return cxpr_eval_error(
            err,
            CXPR_ERR_UNKNOWN_FUNCTION,
            cxpr_eval_producer_unknown_function_message(ast ? ast->data.producer_access.name : NULL));
    }
    if (!cxpr_eval_bind_call_args(ast, entry, ordered_args, err)) {
        return cxpr_num(NAN);
    }

    if (entry->model_producer) {
        return entry->model_producer(ast, ctx, reg, entry->model_producer_userdata, err);
    }

    if (entry->defined_return_field_count > 0u) {
        cxpr_context* tmp = cxpr_context_overlay_new(ctx);
        cxpr_value* fields;
        cxpr_struct_value* record;
        cxpr_value result;

        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        for (size_t i = 0; i < entry->defined_param_count; ++i) {
            cxpr_value arg = cxpr_eval_node(ordered_args[i], ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return cxpr_num(NAN);
            }
            if (arg.type == CXPR_VALUE_NUMBER) {
                cxpr_context_set(tmp, entry->defined_param_names[i], arg.d);
            } else {
                cxpr_context_set_value(tmp, entry->defined_param_names[i], &arg);
            }
            cxpr_value_free(&arg);
        }

        fields = (cxpr_value*)calloc(entry->defined_return_field_count, sizeof(cxpr_value));
        if (!fields) {
            cxpr_context_free(tmp);
            return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        }
        for (size_t i = 0; i < entry->defined_return_field_count; ++i) {
            fields[i] = cxpr_eval_node(entry->defined_return_field_bodies[i], tmp, reg, err);
            if (err && err->code != CXPR_OK) {
                for (size_t j = 0; j <= i; ++j) cxpr_value_free(&fields[j]);
                free(fields);
                cxpr_context_free(tmp);
                return cxpr_num(NAN);
            }
        }
        record = cxpr_struct_value_new((const char* const*)entry->defined_return_field_names,
                                       fields, entry->defined_return_field_count);
        for (size_t i = 0; i < entry->defined_return_field_count; ++i) {
            cxpr_value_free(&fields[i]);
        }
        free(fields);
        cxpr_context_free(tmp);
        if (!record) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        result = cxpr_struct_get_field(record, ast->data.producer_access.field, &found);
        if (found) result = cxpr_value_clone(&result);
        cxpr_struct_value_free(record);
        if (!found) {
            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
        }
        return result;
    }

    const_key = cxpr_eval_prepare_const_key_for_producer(ast,
                                                         ordered_args,
                                                         ast->data.producer_access.argc,
                                                         ctx,
                                                         reg,
                                                         const_key_local,
                                                         sizeof(const_key_local),
                                                         &const_key_heap,
                                                         err);
    if (const_key) {
        produced = cxpr_context_get_cached_struct(ctx, const_key);
        if (!produced) {
            produced = cxpr_eval_struct_result(entry, ast->data.producer_access.name,
                                               ordered_args,
                                               ast->data.producer_access.argc,
                                               const_key,
                                               ctx, reg, err);
        }
    } else {
        produced = cxpr_eval_struct_result(entry, ast->data.producer_access.name,
                                           ordered_args,
                                           ast->data.producer_access.argc,
                                           NULL,
                                           ctx, reg, err);
    }
    free(const_key_heap);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);

    if (ast->data.producer_access.cached_field_index_valid) {
        cxpr_value cached =
            cxpr_struct_get_field_by_index(produced,
                                           ast->data.producer_access.cached_field_index,
                                           &found);
        if (found &&
            strcmp(produced->field_names[ast->data.producer_access.cached_field_index],
                   ast->data.producer_access.field) == 0) {
            return cached;
        }
        mutable_ast->data.producer_access.cached_field_index_valid = false;
    }

    for (size_t i = 0; i < produced->field_count; ++i) {
        if (strcmp(produced->field_names[i], ast->data.producer_access.field) == 0) {
            mutable_ast->data.producer_access.cached_field_index = i;
            mutable_ast->data.producer_access.cached_field_index_valid = true;
            return cxpr_struct_get_field_by_index(produced, i, &found);
        }
    }

    return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
}
