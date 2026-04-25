/**
 * @file calls.c
 * @brief IR runtime helpers for producers and defined functions.
 */

#include "internal.h"
#include <math.h>

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
        return cxpr_fv_double(NAN);
    }
    if (argc > CXPR_MAX_CALL_ARGS || entry->fields_per_arg > CXPR_MAX_PRODUCER_FIELDS) {
        return cxpr_ir_runtime_error(err, "Producer arity too large");
    }

    if (resolved_cache_key) {
        existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
        if (existing) {
            return cxpr_fv_struct((cxpr_struct_value*)existing);
        }
    }

    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_ir_require_type(stack_args[i], CXPR_VALUE_NUMBER, err,
                                  "Producer arguments must be doubles")) {
            return cxpr_fv_double(NAN);
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
            return cxpr_fv_double(NAN);
        }
    }

    existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
    if (existing) {
        free(cache_key_heap);
        return cxpr_fv_struct((cxpr_struct_value*)existing);
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
        return cxpr_fv_double(NAN);
    }
    cxpr_context_set_cached_struct(mutable_ctx, resolved_cache_key, produced);
    cxpr_struct_value_free(produced);
    existing = cxpr_context_get_cached_struct(ctx, resolved_cache_key);
    free(cache_key_heap);
    return cxpr_fv_struct((cxpr_struct_value*)existing);
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
    if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
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
        return cxpr_fv_double(NAN);
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
            return cxpr_fv_double(NAN);
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
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       const cxpr_value* args,
                                       size_t argc, cxpr_error* err) {
    double locals[CXPR_MAX_CALL_ARGS];
    if (!entry || !entry->defined_body) {
        return cxpr_ir_runtime_error(err, "NULL IR defined function entry");
    }
    if (argc != entry->defined_param_count) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Wrong number of arguments";
        }
        return cxpr_fv_double(NAN);
    }

    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type != CXPR_VALUE_NUMBER) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Defined function arguments must be doubles";
            }
            return cxpr_fv_double(NAN);
        }
        locals[i] = args[i].d;
    }

    if (cxpr_ir_prepare_defined_program(entry, reg, err) && entry->defined_program) {
        return cxpr_fv_double(cxpr_ir_exec_with_locals(&entry->defined_program->ir, ctx, reg,
                                                       locals, argc, err));
    }

    {
        cxpr_context* tmp = cxpr_context_overlay_new(ctx);
        if (!tmp) {
            if (err) {
                err->code = CXPR_ERR_OUT_OF_MEMORY;
                err->message = "Out of memory";
            }
            return cxpr_fv_double(NAN);
        }
        for (size_t i = 0; i < argc; ++i) {
            cxpr_context_set(tmp, entry->defined_param_names[i], locals[i]);
        }
        {
            cxpr_value result = {0};
            (void)cxpr_eval_ast(entry->defined_body, tmp, reg, &result, err);
            cxpr_context_free(tmp);
            return result;
        }
    }
}
