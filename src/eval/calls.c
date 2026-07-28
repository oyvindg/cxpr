/**
 * @file calls.c
 * @brief Evaluator call binding, defined functions, and producer helpers.
 */

#include "internal.h"
#include "core.h"
#include "call/args.h"
#include "ir/exec/internal.h"
#include "ir/internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void cxpr_eval_wrap_defined_function_error(cxpr_func_entry* entry, cxpr_error* err) {
    static CXPR_THREAD_LOCAL char message[1024];
    char detail[512];

    if (!entry || !entry->name || !err || err->code == CXPR_OK) return;
    snprintf(detail, sizeof(detail), "%s", err->message ? err->message : cxpr_error_string(err->code));
    snprintf(
        message,
        sizeof(message),
        "Function '%s' eval failed: %s",
        entry->name,
        detail);
    err->message = message;
}

cxpr_value cxpr_eval_struct_producer(cxpr_func_entry* entry, const char* name,
                                     const char* field,
                                     const cxpr_expr_ast* const* arg_nodes,
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

double cxpr_eval_scalar_arg(const cxpr_expr_ast* ast, const cxpr_context* ctx,
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

bool cxpr_eval_bind_call_args(const cxpr_expr_ast* call_ast,
                              const cxpr_func_entry* entry,
                              const cxpr_expr_ast** out_args,
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
                                                   const cxpr_expr_ast* const* ordered_args,
                                                   const cxpr_context* ctx,
                                                   cxpr_value* out);
static bool cxpr_eval_defined_call_can_inline_struct_args(
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args,
    const cxpr_context* ctx);
static bool cxpr_eval_defined_call_can_inline_value_args(
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args);
static cxpr_expr_ast* cxpr_eval_substitute_defined_args(
    const cxpr_expr_ast* ast,
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args);

cxpr_value cxpr_eval_defined_function(cxpr_func_entry* entry,
                                      const cxpr_expr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      cxpr_error* err) {
    const size_t argc = call_ast->data.function_call.argc;
    const cxpr_expr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
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

    if (entry->defined_body &&
        cxpr_eval_defined_call_can_inline_value_args(entry, ordered_args)) {
        cxpr_expr_ast* inlined = cxpr_eval_substitute_defined_args(
            entry->defined_body, entry, ordered_args);
        cxpr_value result;
        if (!inlined) {
            return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        }
        result = cxpr_eval_node(inlined, ctx, reg, err);
        cxpr_expr_ast_free(inlined);
        if (err && err->code != CXPR_OK) cxpr_eval_wrap_defined_function_error(entry, err);
        return result;
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
        if (entry->defined_body &&
            cxpr_eval_defined_call_can_inline_struct_args(entry, ordered_args, ctx)) {
            cxpr_expr_ast* inlined = cxpr_eval_substitute_defined_args(
                entry->defined_body, entry, ordered_args);
            cxpr_value result;
            if (!inlined) {
                return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            }
            result = cxpr_eval_node(inlined, ctx, reg, err);
            cxpr_expr_ast_free(inlined);
            if (err && err->code != CXPR_OK) cxpr_eval_wrap_defined_function_error(entry, err);
            return result;
        }
    }

    if (scalar_only) {
        for (size_t i = 0; i < entry->defined_param_count; i++) {
            const cxpr_expr_ast* arg = ordered_args[i];
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
            /*
             * Keep the parameter overlay for defined functions. The IR fast
             * path is being phased out and can lose local parameter bindings
             * for boolean functions; AST evaluation preserves the source
             * semantics here.
             */
            scalar_only = false;
        } else {
            scalar_only = false;
        }
    } else if (!scalar_only) {
        tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) return cxpr_eval_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");

        for (size_t i = 0; i < entry->defined_param_count; i++) {
            const char* pname = entry->defined_param_names[i];
            const cxpr_expr_ast* arg = ordered_args[i];

            if (entry->defined_param_fields[i] &&
                entry->defined_param_field_counts[i] > 0) {
                cxpr_value struct_arg = {0};
                bool has_struct_arg = false;

                if (arg->type != CXPR_NODE_IDENTIFIER) {
                    struct_arg = cxpr_eval_node(arg, ctx, reg, err);
                    if (err && err->code != CXPR_OK) {
                        cxpr_context_free(tmp);
                        return cxpr_num(NAN);
                    }
                    if (!cxpr_require_type(struct_arg, CXPR_VALUE_STRUCT, err,
                                           "Struct argument must be a struct")) {
                        cxpr_value_free(&struct_arg);
                        cxpr_context_free(tmp);
                        return cxpr_num(NAN);
                    }
                    has_struct_arg = true;
                }

                for (size_t f = 0; f < entry->defined_param_field_counts[i]; f++) {
                    bool found = false;
                    cxpr_value value =
                        has_struct_arg
                            ? cxpr_struct_get_field(
                                  struct_arg.s,
                                  entry->defined_param_fields[i][f],
                                  &found)
                            : cxpr_context_get_field(
                                  ctx,
                                  arg->data.identifier.name,
                                  entry->defined_param_fields[i][f],
                                  &found);
                    char dst_key[256];
                    char src_key[256];

                    if (!found && !has_struct_arg) {
                        double fallback;
                        snprintf(src_key, sizeof(src_key), "%s.%s", arg->data.identifier.name,
                                 entry->defined_param_fields[i][f]);
                        fallback = cxpr_context_get(ctx, src_key, &found);
                        if (!found) {
                            snprintf(src_key,
                                     sizeof(src_key),
                                     "%s_%s",
                                     arg->data.identifier.name,
                                     entry->defined_param_fields[i][f]);
                            fallback = cxpr_context_get(ctx, src_key, &found);
                        }
                        if (!found) {
                            cxpr_context_free(tmp);
                            return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                                   "Unknown struct field");
                        }
                        value = cxpr_num(fallback);
                    }
                    if (!found) {
                        cxpr_value_free(&struct_arg);
                        cxpr_context_free(tmp);
                        return cxpr_eval_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                               "Unknown struct field");
                    }
                    if (!cxpr_require_type(value, CXPR_VALUE_NUMBER, err,
                                           "Struct function arguments must be scalar doubles")) {
                        cxpr_value_free(&value);
                        cxpr_value_free(&struct_arg);
                        cxpr_context_free(tmp);
                        return cxpr_num(NAN);
                    }

                    snprintf(dst_key, sizeof(dst_key), "%s.%s", pname,
                             entry->defined_param_fields[i][f]);
                    cxpr_context_set(tmp, dst_key, value.d);
                    cxpr_value_free(&value);
                }
                cxpr_value_free(&struct_arg);
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
            if (entry->defined_program->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
                bool bool_value = false;
                if (!cxpr_ir_exec_bool_fast(&entry->defined_program->ir, ctx, reg,
                                            scalar_locals,
                                            entry->defined_param_count,
                                            &bool_value, err)) {
                    cxpr_eval_wrap_defined_function_error(entry, err);
                    return cxpr_num(NAN);
                }
                return cxpr_bool(bool_value);
            }
            {
                double result = cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg,
                                                         scalar_locals,
                                                         entry->defined_param_count, err);
                if (err && err->code != CXPR_OK) cxpr_eval_wrap_defined_function_error(entry, err);
                return cxpr_num(result);
            }
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
        if (err && err->code != CXPR_OK) cxpr_eval_wrap_defined_function_error(entry, err);
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

        char separator;

        if (!key) continue;
        if (strncmp(key, src_prefix, src_prefix_len) != 0) continue;
        separator = key[src_prefix_len];
        if (separator != '.' && separator != '_') continue;
        if (dst_prefix_len + 1u + strlen(key + src_prefix_len + 1u) >= sizeof(dst_key)) continue;

        snprintf(dst_key, sizeof(dst_key), "%s.%s",
                 dst_prefix, key + src_prefix_len + 1u);
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
                                                   const cxpr_expr_ast* const* ordered_args,
                                                   const cxpr_context* ctx,
                                                   cxpr_value* out) {
    const cxpr_expr_ast* body;
    const char* body_param;
    const char* body_field;

    if (!entry || !ordered_args || !ctx || !out) return false;
    body = entry->defined_body;
    if (!body) return false;
    if (body->type == CXPR_NODE_FIELD_ACCESS && !body->data.field_access.base) {
        body_param = body->data.field_access.object;
        body_field = body->data.field_access.field;
    } else if (body->type == CXPR_NODE_CHAIN_ACCESS && body->data.chain_access.depth == 2) {
        body_param = body->data.chain_access.path[0];
        body_field = body->data.chain_access.path[1];
    } else {
        return false;
    }

    for (size_t i = 0; i < entry->defined_param_count; ++i) {
        const cxpr_expr_ast* arg = ordered_args[i];
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

static const cxpr_expr_ast* cxpr_eval_defined_arg_for_name(
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args,
    const char* name,
    size_t* out_index) {
    if (out_index) *out_index = (size_t)-1;
    if (!entry || !ordered_args || !name) return NULL;
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_names[i] &&
            strcmp(entry->defined_param_names[i], name) == 0) {
            if (out_index) *out_index = i;
            return ordered_args[i];
        }
    }
    return NULL;
}

static bool cxpr_eval_defined_call_can_inline_struct_args(
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args,
    const cxpr_context* ctx) {
    if (!entry || !ordered_args || !entry->defined_body ||
        entry->defined_return_field_count > 0u) {
        return false;
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields[i] &&
            entry->defined_param_field_counts[i] > 0u) {
            const cxpr_expr_ast* arg = ordered_args[i];
            const char* name;
            if (!arg || arg->type != CXPR_NODE_IDENTIFIER) return false;
            name = arg->data.identifier.name;
            if (!cxpr_context_get_struct(ctx, name) &&
                !cxpr_context_get_cached_struct(ctx, name)) {
                return false;
            }
        }
    }
    return true;
}

static bool cxpr_eval_defined_inline_arg_is_stable(const cxpr_expr_ast* ast) {
    if (!ast) return false;
    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_BOOL:
    case CXPR_NODE_STRING:
        return true;
    default:
        return false;
    }
}

static bool cxpr_eval_defined_call_can_inline_value_args(
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args) {
    if (!entry || !ordered_args || !entry->defined_body ||
        entry->defined_return_field_count > 0u) {
        return false;
    }
    for (size_t i = 0u; i < entry->defined_param_count; ++i) {
        if (entry->defined_param_fields &&
            entry->defined_param_fields[i] &&
            entry->defined_param_field_counts[i] > 0u) {
            return false;
        }
        if (!cxpr_eval_defined_inline_arg_is_stable(ordered_args[i])) {
            return false;
        }
    }
    return true;
}

static cxpr_expr_ast* cxpr_eval_clone_chain_with_replaced_root(
    const cxpr_expr_ast* ast,
    const cxpr_expr_ast* root_arg) {
    const char** path;
    cxpr_expr_ast* cloned;

    if (!ast || ast->type != CXPR_NODE_CHAIN_ACCESS || !root_arg) return NULL;
    if (root_arg->type != CXPR_NODE_IDENTIFIER) {
        if (ast->data.chain_access.depth == 2u) {
            cxpr_expr_ast* base = cxpr_eval_clone_ast(root_arg);
            if (!base) return NULL;
            return cxpr_expr_ast_field_expr_new(base, ast->data.chain_access.path[1]);
        }
        return cxpr_eval_clone_ast(ast);
    }

    path = (const char**)calloc(ast->data.chain_access.depth, sizeof(*path));
    if (!path) return NULL;
    path[0] = root_arg->data.identifier.name;
    for (size_t i = 1u; i < ast->data.chain_access.depth; ++i) {
        path[i] = ast->data.chain_access.path[i];
    }
    cloned = cxpr_expr_ast_new_chain_access(path, ast->data.chain_access.depth);
    free(path);
    return cloned;
}

static cxpr_expr_ast* cxpr_eval_substitute_defined_args(
    const cxpr_expr_ast* ast,
    const cxpr_func_entry* entry,
    const cxpr_expr_ast* const* ordered_args) {
    if (!ast) return NULL;

    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER: {
        const cxpr_expr_ast* mapped = cxpr_eval_defined_arg_for_name(
            entry, ordered_args, ast->data.identifier.name, NULL);
        return cxpr_eval_clone_ast(mapped ? mapped : ast);
    }
    case CXPR_NODE_FIELD_ACCESS:
        if (!ast->data.field_access.base) {
            const cxpr_expr_ast* mapped = cxpr_eval_defined_arg_for_name(
                entry, ordered_args, ast->data.field_access.object, NULL);
            if (mapped) {
                if (mapped->type == CXPR_NODE_IDENTIFIER) {
                    return cxpr_expr_ast_field_new(
                        mapped->data.identifier.name,
                        ast->data.field_access.field);
                }
                {
                    cxpr_expr_ast* base = cxpr_eval_clone_ast(mapped);
                    if (!base) return NULL;
                    return cxpr_expr_ast_field_expr_new(
                        base, ast->data.field_access.field);
                }
            }
        }
        break;
    case CXPR_NODE_CHAIN_ACCESS:
        if (ast->data.chain_access.depth > 0u) {
            const cxpr_expr_ast* mapped = cxpr_eval_defined_arg_for_name(
                entry, ordered_args, ast->data.chain_access.path[0], NULL);
            if (mapped) return cxpr_eval_clone_chain_with_replaced_root(ast, mapped);
        }
        break;
    default:
        break;
    }

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_BOOL:
    case CXPR_NODE_STRING:
    case CXPR_NODE_VARIABLE:
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_eval_clone_ast(ast);
    case CXPR_NODE_FIELD_ACCESS:
        if (ast->data.field_access.base) {
            cxpr_expr_ast* base = cxpr_eval_substitute_defined_args(
                ast->data.field_access.base, entry, ordered_args);
            if (!base) return NULL;
            return cxpr_expr_ast_field_expr_new(base, ast->data.field_access.field);
        }
        return cxpr_eval_clone_ast(ast);
    case CXPR_NODE_ARRAY: {
        cxpr_expr_ast** elements = NULL;
        if (ast->data.array.count > 0u) {
            elements = (cxpr_expr_ast**)calloc(ast->data.array.count, sizeof(*elements));
            if (!elements) return NULL;
            for (size_t i = 0u; i < ast->data.array.count; ++i) {
                elements[i] = cxpr_eval_substitute_defined_args(
                    ast->data.array.elements[i], entry, ordered_args);
                if (!elements[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(elements[j]);
                    free(elements);
                    return NULL;
                }
            }
        }
        return cxpr_expr_ast_array_new(elements, ast->data.array.count);
    }
    case CXPR_NODE_RECORD: {
        cxpr_expr_ast** values = NULL;
        if (ast->data.record.field_count > 0u) {
            values = (cxpr_expr_ast**)calloc(ast->data.record.field_count, sizeof(*values));
            if (!values) return NULL;
            for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
                values[i] = cxpr_eval_substitute_defined_args(
                    ast->data.record.field_values[i], entry, ordered_args);
                if (!values[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(values[j]);
                    free(values);
                    return NULL;
                }
            }
        }
        return cxpr_expr_ast_record_new((const char* const*)ast->data.record.field_names,
                                   values,
                                   ast->data.record.field_count);
    }
    case CXPR_NODE_UNARY_OP: {
        cxpr_expr_ast* operand = cxpr_eval_substitute_defined_args(
            ast->data.unary_op.operand, entry, ordered_args);
        if (!operand) return NULL;
        return cxpr_expr_ast_unary_new(ast->data.unary_op.op, operand);
    }
    case CXPR_NODE_BINARY_OP: {
        cxpr_expr_ast* left = cxpr_eval_substitute_defined_args(
            ast->data.binary_op.left, entry, ordered_args);
        cxpr_expr_ast* right = cxpr_eval_substitute_defined_args(
            ast->data.binary_op.right, entry, ordered_args);
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
        if (ast->data.function_call.argc > 0u) {
            args = (cxpr_expr_ast**)calloc(ast->data.function_call.argc, sizeof(*args));
            arg_names = (char**)calloc(ast->data.function_call.argc, sizeof(*arg_names));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
                args[i] = cxpr_eval_substitute_defined_args(
                    ast->data.function_call.args[i], entry, ordered_args);
                if (!args[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(args[j]);
                    for (size_t j = 0u; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.function_call.arg_names &&
                    ast->data.function_call.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.function_call.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0u; j <= i; ++j) cxpr_expr_ast_free(args[j]);
                        for (size_t j = 0u; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_expr_ast_call_named_new(
            ast->data.function_call.name, args, arg_names, ast->data.function_call.argc);
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        cxpr_expr_ast** args = NULL;
        char** arg_names = NULL;
        if (ast->data.producer_access.argc > 0u) {
            args = (cxpr_expr_ast**)calloc(ast->data.producer_access.argc, sizeof(*args));
            arg_names = (char**)calloc(ast->data.producer_access.argc, sizeof(*arg_names));
            if (!args || !arg_names) {
                free(args);
                free(arg_names);
                return NULL;
            }
            for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) {
                args[i] = cxpr_eval_substitute_defined_args(
                    ast->data.producer_access.args[i], entry, ordered_args);
                if (!args[i]) {
                    for (size_t j = 0u; j < i; ++j) cxpr_expr_ast_free(args[j]);
                    for (size_t j = 0u; j < i; ++j) free(arg_names[j]);
                    free(args);
                    free(arg_names);
                    return NULL;
                }
                if (ast->data.producer_access.arg_names &&
                    ast->data.producer_access.arg_names[i]) {
                    arg_names[i] = cxpr_strdup(ast->data.producer_access.arg_names[i]);
                    if (!arg_names[i]) {
                        for (size_t j = 0u; j <= i; ++j) cxpr_expr_ast_free(args[j]);
                        for (size_t j = 0u; j < i; ++j) free(arg_names[j]);
                        free(args);
                        free(arg_names);
                        return NULL;
                    }
                }
            }
        }
        return cxpr_expr_ast_producer_field_named_new(
            ast->data.producer_access.name,
            args,
            arg_names,
            ast->data.producer_access.argc,
            ast->data.producer_access.field);
    }
    case CXPR_NODE_LOOKBACK: {
        cxpr_expr_ast* target = cxpr_eval_substitute_defined_args(
            ast->data.lookback.target, entry, ordered_args);
        cxpr_expr_ast* index = cxpr_eval_substitute_defined_args(
            ast->data.lookback.index, entry, ordered_args);
        if (!target || !index) {
            cxpr_expr_ast_free(target);
            cxpr_expr_ast_free(index);
            return NULL;
        }
        return cxpr_expr_ast_lookback_new(target, index);
    }
    case CXPR_NODE_TERNARY: {
        cxpr_expr_ast* condition = cxpr_eval_substitute_defined_args(
            ast->data.ternary.condition, entry, ordered_args);
        cxpr_expr_ast* yes = cxpr_eval_substitute_defined_args(
            ast->data.ternary.true_branch, entry, ordered_args);
        cxpr_expr_ast* no = cxpr_eval_substitute_defined_args(
            ast->data.ternary.false_branch, entry, ordered_args);
        if (!condition || !yes || !no) {
            cxpr_expr_ast_free(condition);
            cxpr_expr_ast_free(yes);
            cxpr_expr_ast_free(no);
            return NULL;
        }
        return cxpr_expr_ast_ternary_new(condition, yes, no);
    }
    case CXPR_NODE_IDENTIFIER:
        break;
    }

    return cxpr_eval_clone_ast(ast);
}

cxpr_value cxpr_eval_defined_with_overlay(cxpr_func_entry* entry,
                                          const cxpr_expr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          cxpr_error* err) {
    cxpr_context* tmp;
    const cxpr_expr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
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
        const cxpr_expr_ast* arg = ordered_args[i];

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
        if (err && err->code != CXPR_OK) cxpr_eval_wrap_defined_function_error(entry, err);
        return result;
    }
}
