/**
 * @file calls.c
 * @brief IR runtime helpers for producers and defined functions.
 */

#include "internal.h"
#include "../../eval/internal.h"
#include <math.h>
#include <stdio.h>

static void cxpr_ir_wrap_defined_function_error(cxpr_func_entry* entry, cxpr_error* err) {
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

static bool cxpr_ir_call_instr_memoable(const cxpr_ir_instr* instr) {
    const cxpr_ast* ast = instr ? (const cxpr_ast*)instr->payload : NULL;

    if (!instr || !instr->func || !ast || ast->type != CXPR_NODE_FUNCTION_CALL) return false;
    return !instr->func->ast_func_handler &&
           !instr->func->ast_func &&
           !(instr->func->struct_producer &&
             !instr->func->sync_func &&
             !instr->func->value_func);
}

bool cxpr_ir_call_memo_get(const cxpr_context* ctx,
                           const cxpr_ir_instr* instr,
                           cxpr_value* out) {
    const cxpr_ast* ast = instr ? (const cxpr_ast*)instr->payload : NULL;

    if (!cxpr_ir_call_instr_memoable(instr)) return false;
    return cxpr_eval_memo_get(ctx, ast, cxpr_eval_function_call_hash_cached(ast), out);
}

bool cxpr_ir_call_memo_set(const cxpr_context* ctx,
                           const cxpr_ir_instr* instr,
                           cxpr_value value) {
    const cxpr_ast* ast = instr ? (const cxpr_ast*)instr->payload : NULL;

    if (!cxpr_ir_call_instr_memoable(instr)) return false;
    return cxpr_eval_memo_set(ctx, ast, cxpr_eval_function_call_hash_cached(ast), value);
}

cxpr_value cxpr_ir_call_producer_cached(cxpr_func_entry* entry, const char* name,
                                        const char* cache_key,
                                        const cxpr_context* ctx,
                                        const cxpr_value* stack_args,
                                        size_t argc, cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_value outputs[CXPR_MAX_PRODUCER_FIELDS];
    cxpr_struct_value* produced;
    double args[CXPR_MAX_CALL_ARGS];
    char cache_key_local[256];
    char* cache_key_heap = NULL;
    const char* resolved_cache_key = cache_key;

    if (!entry || !entry->struct_producer) {
        return cxpr_ir_runtime_error(err, "Invalid producer opcode");
    }

    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_num(NAN);
    }
    if (argc > CXPR_MAX_CALL_ARGS || entry->fields_per_arg > CXPR_MAX_PRODUCER_FIELDS) {
        return cxpr_ir_runtime_error(err, "Producer arity too large");
    }

    if (resolved_cache_key) {
        existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
        if (existing) {
            return cxpr_struct((cxpr_struct_value*)existing);
        }
    }

    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_ir_require_type(stack_args[i], CXPR_VALUE_NUMBER, err,
                                  "Producer arguments must be doubles")) {
            return cxpr_num(NAN);
        }
        args[i] = stack_args[i].d;
    }

    if (!resolved_cache_key) {
        resolved_cache_key = cxpr_ir_build_struct_cache_key(name, args, argc,
                                                            cache_key_local,
                                                            sizeof(cache_key_local),
                                                            &cache_key_heap);
        if (!resolved_cache_key) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }
    }

    existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
    if (existing) {
        free(cache_key_heap);
        return cxpr_struct((cxpr_struct_value*)existing);
    }

    entry->struct_producer(args, argc, outputs, entry->fields_per_arg, entry->userdata);
    produced = cxpr_struct_value_new((const char* const*)entry->struct_fields,
                                     outputs, entry->fields_per_arg);
    if (!produced) {
        free(cache_key_heap);
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return cxpr_num(NAN);
    }
    cxpr_context_set_cached_struct(mutable_ctx, resolved_cache_key, produced);
    cxpr_struct_value_free(produced);
    existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
    free(cache_key_heap);
    return cxpr_struct((cxpr_struct_value*)existing);
}

cxpr_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                 const cxpr_context* ctx,
                                 const cxpr_value* stack_args,
                                 size_t argc, cxpr_error* err) {
    return cxpr_ir_call_producer_cached(entry, name, NULL, ctx, stack_args, argc, err);
}

cxpr_value cxpr_ir_call_producer_field_cached(cxpr_func_entry* entry,
                                              const char* name,
                                              const char* cache_key,
                                              const cxpr_context* ctx,
                                              const cxpr_value* stack_args,
                                              size_t argc,
                                              const char* field,
                                              cxpr_error* err) {
    cxpr_value produced;
    bool found = false;

    produced = cxpr_ir_call_producer_cached(entry, name, cache_key, ctx, stack_args, argc, err);
    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
    if (produced.type != CXPR_VALUE_STRUCT) {
        return cxpr_ir_runtime_error(err, "Field access requires struct operand");
    }

    produced = cxpr_ir_struct_get_field(produced.s, field, &found);
    if (!found) return cxpr_ir_make_not_found(err, "Unknown field access");
    return produced;
}

cxpr_value cxpr_ir_call_producer_field(cxpr_func_entry* entry, const char* name,
                                       const cxpr_context* ctx,
                                       const cxpr_value* stack_args,
                                       size_t argc, const char* field,
                                       cxpr_error* err) {
    return cxpr_ir_call_producer_field_cached(entry, name, NULL, ctx, stack_args, argc,
                                              field, err);
}

cxpr_value cxpr_ir_call_producer_const_field(cxpr_func_entry* entry,
                                             const char* cache_key,
                                             const cxpr_context* ctx,
                                             const double* const_args,
                                             size_t argc,
                                             const char* field,
                                             cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_value outputs[CXPR_MAX_PRODUCER_FIELDS];
    cxpr_struct_value* produced;
    cxpr_value value;
    bool found = false;

    if (!entry || !entry->struct_producer) {
        return cxpr_ir_runtime_error(err, "Invalid producer opcode");
    }
    if (argc < entry->min_args || argc > entry->max_args) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_num(NAN);
    }
    if (argc > CXPR_MAX_CALL_ARGS || entry->fields_per_arg > CXPR_MAX_PRODUCER_FIELDS) {
        return cxpr_ir_runtime_error(err, "Producer arity too large");
    }

    existing = cxpr_context_get_cached_struct(ctx, cache_key);
    if (!existing) {
        entry->struct_producer(const_args, argc, outputs, entry->fields_per_arg, entry->userdata);
        produced = cxpr_struct_value_new((const char* const*)entry->struct_fields,
                                         outputs, entry->fields_per_arg);
        if (!produced) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }
        cxpr_context_set_cached_struct(mutable_ctx, cache_key, produced);
        cxpr_struct_value_free(produced);
        existing = cxpr_context_get_cached_struct(ctx, cache_key);
    }

    value = cxpr_ir_struct_get_field(existing, field, &found);
    if (!found) return cxpr_ir_make_not_found(err, "Unknown field access");
    return value;
}

cxpr_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                       const cxpr_ast* call_ast,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       const cxpr_value* args,
                                       size_t argc, cxpr_error* err) {
    double locals[CXPR_MAX_CALL_ARGS];
    bool scalar_only = true;
    if (!entry || (!entry->defined_body && entry->defined_return_field_count == 0u)) {
        return cxpr_ir_runtime_error(err, "NULL IR defined function entry");
    }
    if (argc != entry->defined_param_count) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_num(NAN);
    }

    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type == CXPR_VALUE_NUMBER) {
            locals[i] = args[i].d;
        } else {
            scalar_only = false;
        }
    }

    if (entry->defined_return_field_count > 0u) {
        cxpr_context* tmp = cxpr_context_overlay_new(ctx);
        cxpr_value* fields;
        cxpr_struct_value* record;
        cxpr_value result;

        if (!tmp) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }
        for (size_t i = 0; i < argc; ++i) {
            const cxpr_ast* arg_ast =
                (call_ast &&
                 call_ast->type == CXPR_NODE_FUNCTION_CALL &&
                 i < call_ast->data.function_call.argc)
                    ? call_ast->data.function_call.args[i]
                    : NULL;
            if (entry->defined_param_fields &&
                entry->defined_param_fields[i] &&
                entry->defined_param_field_counts[i] > 0u &&
                args[i].type != CXPR_VALUE_STRUCT &&
                arg_ast &&
                arg_ast->type == CXPR_NODE_IDENTIFIER) {
                const char* root = arg_ast->data.identifier.name;
                for (size_t f = 0u; f < entry->defined_param_field_counts[i]; ++f) {
                    bool found = false;
                    char key[256];
                    double field_value;
                    snprintf(key, sizeof(key), "%s.%s", root, entry->defined_param_fields[i][f]);
                    field_value = cxpr_context_get(ctx, key, &found);
                    if (!found) {
                        snprintf(key, sizeof(key), "%s_%s", root, entry->defined_param_fields[i][f]);
                        field_value = cxpr_context_get(ctx, key, &found);
                    }
                    if (!found) {
                        if (err) {
                            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
                            err->message = "Unknown struct field";
                        }
                        cxpr_context_free(tmp);
                        return cxpr_num(NAN);
                    }
                    snprintf(key,
                             sizeof(key),
                             "%s.%s",
                             entry->defined_param_names[i],
                             entry->defined_param_fields[i][f]);
                    cxpr_context_set(tmp, key, field_value);
                }
            } else if (args[i].type == CXPR_VALUE_NUMBER) {
                cxpr_context_set(tmp, entry->defined_param_names[i], args[i].d);
            } else {
                cxpr_context_set_value(tmp, entry->defined_param_names[i], &args[i]);
            }
        }

        fields = (cxpr_value*)calloc(entry->defined_return_field_count, sizeof(cxpr_value));
        if (!fields) {
            cxpr_context_free(tmp);
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }

        for (size_t i = 0; i < entry->defined_return_field_count; ++i) {
            if (!cxpr_eval_ast(entry->defined_return_field_bodies[i], tmp, reg,
                               &fields[i], err)) {
                for (size_t j = 0; j < i; ++j) cxpr_value_free(&fields[j]);
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
        if (!record) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }
        result = cxpr_struct(record);
        return result;
    }

    if (entry->defined_body &&
        call_ast &&
        call_ast->type == CXPR_NODE_FUNCTION_CALL) {
        return cxpr_eval_defined_function(entry, call_ast, ctx, reg, err);
    }

    if (scalar_only &&
        cxpr_ir_prepare_defined_program(entry, reg, err) && entry->defined_program) {
        cxpr_value result = cxpr_ir_exec_value_with_locals(
            &entry->defined_program->ir, ctx, reg, locals, argc, err);
        if (err && err->code != CXPR_OK) cxpr_ir_wrap_defined_function_error(entry, err);
        return result;
    }

    {
        cxpr_context* tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_num(NAN);
        }
        for (size_t i = 0; i < argc; ++i) {
            if (args[i].type == CXPR_VALUE_NUMBER) {
                cxpr_context_set(tmp, entry->defined_param_names[i], args[i].d);
            } else {
                cxpr_context_set_value(tmp, entry->defined_param_names[i], &args[i]);
            }
        }
        {
            cxpr_value result = {0};
            (void)cxpr_eval_ast(entry->defined_body, tmp, reg, &result, err);
            if (err && err->code != CXPR_OK) cxpr_ir_wrap_defined_function_error(entry, err);
            cxpr_context_free(tmp);
            return result;
        }
    }
}
