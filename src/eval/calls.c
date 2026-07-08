/**
 * @file calls.c
 * @brief Evaluator call binding, defined functions, and producer helpers.
 */

#include "internal.h"
#include "core.h"
#include "call/args.h"
#include "ir/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const char* cxpr_eval_calls_unknown_function_message(const char* name) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!name || name[0] == '\0') return "Unknown function";
    snprintf(message, sizeof(message), "Unknown function '%s'", name);
    return message;
}

cxpr_value cxpr_eval_struct_producer(cxpr_func_entry* entry, const char* name,
                                     const char* field,
                                     const cxpr_ast* const* arg_nodes,
                                     size_t argc,
                                     const cxpr_context* ctx,
                                     const cxpr_registry* reg,
                                     cxpr_error* err) {
    const cxpr_struct_value* produced;
    cxpr_value result;
    bool found = false;

    if (!entry || !entry->struct_producer) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
    }

    produced = cxpr_eval_struct_result(entry, name, arg_nodes, argc, NULL, ctx, reg, err);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);

    result = cxpr_struct_get_field(produced, field, &found);
    if (!found) {
        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER, "Unknown field access");
    }
    return result;
}

double cxpr_eval_scalar_arg(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err) {
    cxpr_value value = cxpr_eval_node(ast, ctx, reg, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (!cxpr_require_type(value, CXPR_VALUE_NUMBER, err, "Expected double argument")) {
        return NAN;
    }
    return value.d;
}

cxpr_value cxpr_eval_named_arg_error(cxpr_error* err, cxpr_error_code code,
                                     const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return cxpr_num(NAN);
}

bool cxpr_eval_bind_call_args(const cxpr_ast* call_ast,
                              const cxpr_func_entry* entry,
                              const cxpr_ast** out_args,
                              cxpr_error* err) {
    cxpr_error_code code = CXPR_OK;
    const char* message = NULL;

    if (!call_ast || !entry || !out_args) return false;
    if (!cxpr_call_bind_args(call_ast, entry, out_args, &code, &message)) {
        cxpr_eval_named_arg_error(err, code, message);
        return false;
    }
    return true;
}

static bool cxpr_eval_defined_overlay_direct_field(cxpr_func_entry* entry,
                                                   const cxpr_ast* const* ordered_args,
                                                   const cxpr_context* ctx,
                                                   cxpr_value* out);

cxpr_value cxpr_eval_defined_function(cxpr_func_entry* entry,
                                      const cxpr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      cxpr_error* err) {
    const size_t argc = call_ast->data.function_call.argc;
    const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
    cxpr_context* tmp = NULL;
    double scalar_locals[CXPR_MAX_CALL_ARGS];
    bool scalar_only = (argc <= CXPR_MAX_CALL_ARGS);
    bool needs_catchor_passthrough = false;

    if (argc != entry->defined_param_count) {
        return cxpr_eval_error(err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
    }
    if (!cxpr_eval_bind_call_args(call_ast, entry, ordered_args, err)) {
        return cxpr_num(NAN);
    }

    if (entry->defined_return_field_count > 0u) {
        cxpr_value* fields;
        cxpr_struct_value* record;
        cxpr_value result;

        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        for (size_t i = 0; i < entry->defined_param_count; ++i) {
            cxpr_value value = cxpr_eval_node(ordered_args[i], ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return cxpr_num(NAN);
            }
            if (value.type == CXPR_VALUE_NUMBER) {
                cxpr_context_set(tmp, entry->defined_param_names[i], value.d);
            } else {
                cxpr_context_set_value(tmp, entry->defined_param_names[i], &value);
            }
            cxpr_value_free(&value);
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
        result = cxpr_struct(record);
        return result;
    }

    for (size_t i = 0; i < entry->defined_param_count; i++) {
        if (entry->defined_param_fields[i] && entry->defined_param_field_counts[i] > 0) {
            scalar_only = false;
            break;
        }
    }

    if (!scalar_only) {
        cxpr_value direct_value;
        if (cxpr_eval_defined_overlay_direct_field(entry, ordered_args, ctx, &direct_value)) {
            return direct_value;
        }
    }

    if (scalar_only) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            const cxpr_ast* arg = ordered_args[i];
            if (arg->type == CXPR_NODE_IDENTIFIER) {
                bool found = false;
                (void)cxpr_context_get(ctx, arg->data.identifier.name, &found);
                if (!found) {
                    needs_catchor_passthrough = true;
                    break;
                }
            }
        }
    }

    if (scalar_only && !needs_catchor_passthrough) {
        bool all_numbers = true;
        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            cxpr_value v = cxpr_eval_node(ordered_args[i], ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return cxpr_num(NAN);
            }
            if (v.type == CXPR_VALUE_NUMBER) {
                scalar_locals[i] = v.d;
                cxpr_context_set(tmp, entry->defined_param_names[i], v.d);
            } else {
                all_numbers = false;
                cxpr_context_set_value(tmp, entry->defined_param_names[i], &v);
            }
            cxpr_value_free(&v);
        }
        if (all_numbers) {
            cxpr_context_free(tmp);
            tmp = NULL;
        } else {
            scalar_only = false;
        }
    } else if (!scalar_only) {
        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");

        for (size_t i = 0; i < entry->defined_param_count; i++) {
            const char* pname = entry->defined_param_names[i];
            const cxpr_ast* arg = ordered_args[i];

            if (entry->defined_param_fields[i] &&
                entry->defined_param_field_counts[i] > 0) {
                if (arg->type != CXPR_NODE_IDENTIFIER) {
                    cxpr_context_free(tmp);
                    return cxpr_eval_error(err, CXPR_ERR_SYNTAX,
                                           "Struct argument must be an identifier");
                }

                for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                    bool found = false;
                    cxpr_value value =
                        cxpr_context_get_field(ctx, arg->data.identifier.name,
                                               entry->defined_param_fields[i][f], &found);
                    char dst_key[256];
                    char src_key[256];

                    if (!found) {
                        double fallback;
                        snprintf(src_key, sizeof(src_key), "%s.%s", arg->data.identifier.name,
                                 entry->defined_param_fields[i][f]);
                        fallback = cxpr_context_get(ctx, src_key, &found);
                        if (!found) {
                            cxpr_context_free(tmp);
                            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                                   "Unknown struct field");
                        }
                        value = cxpr_num(fallback);
                    }
                    if (!cxpr_require_type(value, CXPR_VALUE_NUMBER, err,
                                           "Struct function arguments must be scalar doubles")) {
                        cxpr_context_free(tmp);
                        return cxpr_num(NAN);
                    }

                    snprintf(dst_key, sizeof(dst_key), "%s.%s", pname,
                             entry->defined_param_fields[i][f]);
                    cxpr_context_set(tmp, dst_key, value.d);
                }
            } else {
                cxpr_value value = cxpr_eval_node(arg, ctx, reg, err);
                if (err && err->code != CXPR_OK) {
                    cxpr_context_free(tmp);
                    return cxpr_num(NAN);
                }
                if (value.type == CXPR_VALUE_NUMBER) {
                    cxpr_context_set(tmp, pname, value.d);
                } else {
                    cxpr_context_set_value(tmp, pname, &value);
                }
                cxpr_value_free(&value);
            }
        }
    }

    if (needs_catchor_passthrough) {
        return cxpr_eval_defined_with_overlay(entry, call_ast, ctx, reg, err);
    }

    if (scalar_only) {
        if (cxpr_ir_prepare_defined_program(entry, reg, err) && entry->defined_program) {
            return cxpr_num(cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg,
                                                           scalar_locals,
                                                           entry->defined_param_count, err));
        }

        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            cxpr_context_set(tmp, entry->defined_param_names[i], scalar_locals[i]);
        }
    }

    {
        cxpr_value result = cxpr_eval_node(entry->defined_body, tmp ? tmp : ctx, reg, err);
        if (tmp) cxpr_context_free(tmp);
        return result;
    }
}

bool cxpr_context_copy_prefixed_scalars(cxpr_context* dst, const cxpr_context* src,
                                        const char* src_prefix, const char* dst_prefix) {
    bool copied = false;
    size_t src_prefix_len;
    size_t dst_prefix_len;

    if (!dst || !src || !src_prefix || !dst_prefix) return false;

    src_prefix_len = strlen(src_prefix);
    dst_prefix_len = strlen(dst_prefix);

    for (size_t i = 0; i < src->variables.capacity; i++) {
        const char* key = src->variables.entries[i].key;
        char dst_key[256];

        if (!key) continue;
        if (strncmp(key, src_prefix, src_prefix_len) != 0 || key[src_prefix_len] != '.') continue;
        if (dst_prefix_len + strlen(key + src_prefix_len) >= sizeof(dst_key)) continue;

        snprintf(dst_key, sizeof(dst_key), "%s%s", dst_prefix, key + src_prefix_len);
        cxpr_context_set(dst, dst_key, src->variables.entries[i].value);
        copied = true;
    }

    if (src->parent) {
        copied = cxpr_context_copy_prefixed_scalars(dst, src->parent,
                                                    src_prefix, dst_prefix) || copied;
    }
    return copied;
}

static bool cxpr_eval_defined_overlay_direct_field(cxpr_func_entry* entry,
                                                   const cxpr_ast* const* ordered_args,
                                                   const cxpr_context* ctx,
                                                   cxpr_value* out) {
    const cxpr_ast* body;
    const char* body_param;
    const char* body_field;

    if (!entry || !ordered_args || !ctx || !out) return false;
    body = entry->defined_body;
    if (!body) return false;
    if (body->type == CXPR_NODE_FIELD_ACCESS) {
        body_param = body->data.field_access.object;
        body_field = body->data.field_access.field;
    } else if (body->type == CXPR_NODE_CHAIN_ACCESS && body->data.chain_access.depth == 2) {
        body_param = body->data.chain_access.path[0];
        body_field = body->data.chain_access.path[1];
    } else {
        return false;
    }

    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        const cxpr_ast* arg = ordered_args[i];
        const char* arg_name;
        const cxpr_struct_value* s;
        bool found = false;

        if (strcmp(entry->defined_param_names[i], body_param) != 0) continue;
        if (!arg || arg->type != CXPR_NODE_IDENTIFIER) return false;

        arg_name = arg->data.identifier.name;
        s = cxpr_context_get_struct(ctx, arg_name);
        if (!s) s = cxpr_context_get_cached_struct(ctx, arg_name);
        if (s) {
            cxpr_value value = cxpr_struct_get_field(s, body_field, &found);
            if (found) {
                *out = value;
                return true;
            }
        }

        {
            char key[256];
            int written = snprintf(key, sizeof(key), "%s.%s", arg_name, body_field);
            if (written > 0 && (size_t)written < sizeof(key)) {
                double value = cxpr_context_get(ctx, key, &found);
                if (found) {
                    *out = cxpr_num(value);
                    return true;
                }
            }
        }

        return false;
    }

    return false;
}

cxpr_value cxpr_eval_defined_with_overlay(cxpr_func_entry* entry,
                                          const cxpr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          cxpr_error* err) {
    cxpr_context* tmp;
    const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
    cxpr_value direct_value;

    if (!cxpr_eval_bind_call_args(call_ast, entry, ordered_args, err)) {
        return cxpr_num(NAN);
    }

    if (cxpr_eval_defined_overlay_direct_field(entry, ordered_args, ctx, &direct_value)) {
        return direct_value;
    }

    tmp = cxpr_context_overlay_new(ctx);
    if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");

    for (size_t i = 0; i < entry->defined_param_count; i++) {
        const char* pname = entry->defined_param_names[i];
        const cxpr_ast* arg = ordered_args[i];

        if (arg->type == CXPR_NODE_IDENTIFIER) {
            const char* arg_name = arg->data.identifier.name;
            const cxpr_struct_value* s = cxpr_context_get_struct(ctx, arg_name);
            bool found = false;
            double value;

            if (!s) s = cxpr_context_get_cached_struct(ctx, arg_name);
            if (s) {
                cxpr_context_set_struct(tmp, pname, s);
                continue;
            }

            value = cxpr_context_get(ctx, arg_name, &found);
            if (found) {
                cxpr_context_set(tmp, pname, value);
                continue;
            }

            if (cxpr_context_copy_prefixed_scalars(tmp, ctx, arg_name, pname)) {
                continue;
            }
        }

        {
            cxpr_value value = cxpr_eval_node(arg, ctx, reg, err);
            if (err && err->code != CXPR_OK) {
                cxpr_context_free(tmp);
                return cxpr_num(NAN);
            }
            if (value.type == CXPR_VALUE_NUMBER) {
                cxpr_context_set(tmp, pname, value.d);
            } else {
                cxpr_context_set_value(tmp, pname, &value);
            }
            cxpr_value_free(&value);
        }
    }

    {
        cxpr_value result = cxpr_eval_node(entry->defined_body, tmp, reg, err);
        cxpr_context_free(tmp);
        return result;
    }
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

    if (!entry || (!entry->struct_producer && entry->defined_return_field_count == 0u)) {
        return cxpr_eval_error(
            err,
            CXPR_ERR_UNKNOWN_FUNCTION,
            cxpr_eval_calls_unknown_function_message(ast ? ast->data.producer_access.name : NULL));
    }
    if (!cxpr_eval_bind_call_args(ast, entry, ordered_args, err)) {
        return cxpr_num(NAN);
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
