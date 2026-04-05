/**
 * @file ir_exec.c
 * @brief IR execution support for cxpr.
 */

#include "internal.h"
#include <math.h>

static double cxpr_ir_context_get_prehashed(const cxpr_context* ctx, const char* name,
                                            unsigned long hash, bool* found) {
    double value;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    value = cxpr_hashmap_get_prehashed(&ctx->variables, name, hash, found);
    if (found && *found) return value;
    if (ctx->parent) return cxpr_ir_context_get_prehashed(ctx->parent, name, hash, found);
    if (found) *found = false;
    return 0.0;
}

static cxpr_value cxpr_ir_struct_get_field(const cxpr_struct_value* value,
                                                 const char* field, bool* found) {
    if (found) *found = false;
    if (!value || !field) return cxpr_fv_double(NAN);

    for (size_t i = 0; i < value->field_count; ++i) {
        if (strcmp(value->field_names[i], field) == 0) {
            if (found) *found = true;
            return value->field_values[i];
        }
    }

    return cxpr_fv_double(NAN);
}

static cxpr_value cxpr_ir_call_producer_cached(cxpr_func_entry* entry, const char* name,
                                                     const char* cache_key,
                                                     const cxpr_context* ctx,
                                                     const cxpr_value* stack_args,
                                                     size_t argc, cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_value outputs[64];
    cxpr_struct_value* produced;
    double args[32];
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
    if (argc > 32 || entry->fields_per_arg > 64) {
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

static cxpr_value cxpr_ir_call_producer(cxpr_func_entry* entry, const char* name,
                                              const cxpr_context* ctx,
                                              const cxpr_value* stack_args,
                                              size_t argc, cxpr_error* err) {
    return cxpr_ir_call_producer_cached(entry, name, NULL, ctx, stack_args, argc, err);
}

static cxpr_value cxpr_ir_call_producer_field_cached(cxpr_func_entry* entry,
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

static cxpr_value cxpr_ir_call_producer_field(cxpr_func_entry* entry, const char* name,
                                                    const cxpr_context* ctx,
                                                    const cxpr_value* stack_args,
                                                    size_t argc, const char* field,
                                                    cxpr_error* err) {
    return cxpr_ir_call_producer_field_cached(entry, name, NULL, ctx, stack_args, argc,
                                              field, err);
}

static cxpr_value cxpr_ir_call_producer_const_field(cxpr_func_entry* entry,
                                                          const char* cache_key,
                                                          const cxpr_context* ctx,
                                                          const double* const_args,
                                                          size_t argc,
                                                          const char* field,
                                                          cxpr_error* err) {
    cxpr_context* mutable_ctx = (cxpr_context*)ctx;
    const cxpr_struct_value* existing;
    cxpr_value outputs[64];
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
    if (argc > 32 || entry->fields_per_arg > 64) {
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

static cxpr_value cxpr_ir_load_field_value(const cxpr_context* ctx, const cxpr_registry* reg,
                                                 const cxpr_ir_instr* instr, cxpr_error* err) {
    const char* dot;
    bool found = false;
    cxpr_value value;
    char root[128];
    size_t root_len;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid field access");
    }

    dot = strchr(instr->name, '.');
    if (!dot) {
        return cxpr_ir_runtime_error(err, "Malformed field access");
    }

    root_len = (size_t)(dot - instr->name);
    if (root_len == 0 || root_len >= sizeof(root)) {
        return cxpr_ir_runtime_error(err, "Field access root too long");
    }

    memcpy(root, instr->name, root_len);
    root[root_len] = '\0';

    value = cxpr_context_get_field(ctx, root, dot + 1, &found);
    if (found) return value;

    if (reg) {
        cxpr_func_entry* producer = cxpr_registry_find(reg, root);
        if (producer && producer->struct_producer &&
            producer->min_args == 0 && producer->max_args == 0) {
            cxpr_value produced = cxpr_ir_call_producer(producer, root, ctx, NULL, 0, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (produced.type == CXPR_VALUE_STRUCT) {
                value = cxpr_ir_struct_get_field(produced.s, dot + 1, &found);
            } else {
                value = cxpr_context_get_field(ctx, root, dot + 1, &found);
            }
            if (found) return value;
        }
    }

    value = cxpr_fv_double(cxpr_ir_context_get_prehashed(ctx, instr->name, instr->hash, &found));
    if (!found) {
        return cxpr_ir_make_not_found(err, "Unknown field access");
    }
    return value;
}

static cxpr_value cxpr_ir_load_chain_value(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                                 cxpr_error* err) {
    char* path;
    char* segment;
    char* saveptr = NULL;
    const cxpr_struct_value* current;
    cxpr_value value = cxpr_fv_double(NAN);
    bool found = false;

    if (!ctx || !instr || !instr->name) {
        return cxpr_ir_runtime_error(err, "Invalid chain access");
    }

    path = cxpr_strdup(instr->name);
    if (!path) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return cxpr_fv_double(NAN);
    }

    segment = cxpr_strtok_r(path, ".", &saveptr);
    if (!segment) {
        free(path);
        return cxpr_ir_runtime_error(err, "Malformed chain access");
    }

    current = cxpr_context_get_struct(ctx, segment);
    if (!current) {
        cxpr_value root = cxpr_context_get_typed(ctx, segment, &found);
        if (found && root.type == CXPR_VALUE_STRUCT) {
            current = root.s;
        } else {
            free(path);
            return cxpr_ir_make_not_found(err, "Unknown identifier");
        }
    }

    segment = cxpr_strtok_r(NULL, ".", &saveptr);
    while (segment) {
        char* next = cxpr_strtok_r(NULL, ".", &saveptr);
        found = false;
        for (size_t i = 0; i < current->field_count; ++i) {
            if (strcmp(current->field_names[i], segment) == 0) {
                value = current->field_values[i];
                found = true;
                break;
            }
        }
        if (!found) {
            free(path);
            return cxpr_ir_make_not_found(err, "Unknown identifier");
        }
        if (!next) {
            free(path);
            return value;
        }
        if (!cxpr_ir_require_type(value, CXPR_VALUE_STRUCT, err,
                                  "Chained access requires struct intermediate")) {
            free(path);
            return cxpr_fv_double(NAN);
        }
        current = value.s;
        segment = next;
    }

    free(path);
    return cxpr_ir_runtime_error(err, "Malformed chain access");
}

static bool cxpr_ir_push_squared(cxpr_value* stack, size_t* sp, cxpr_value value,
                                 cxpr_error* err) {
    if (!cxpr_ir_require_type(value, CXPR_VALUE_NUMBER, err,
                              "Square operation requires double operand")) {
        return false;
    }
    value.d *= value.d;
    return cxpr_ir_stack_push(stack, sp, value, 64, err);
}

static bool cxpr_ir_pop1(cxpr_value* stack, size_t* sp, cxpr_value* out,
                         cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 1, err)) return false;
    *out = stack[--(*sp)];
    return true;
}

static bool cxpr_ir_pop2(cxpr_value* stack, size_t* sp, cxpr_value* left,
                         cxpr_value* right, cxpr_error* err) {
    if (!cxpr_ir_require_stack(*sp, 2, err)) return false;
    *right = stack[--(*sp)];
    *left = stack[--(*sp)];
    return true;
}

static cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                                           const cxpr_registry* reg, const double* locals,
                                           size_t local_count, cxpr_error* err);

static cxpr_value cxpr_ir_call_defined_scalar(cxpr_func_entry* entry,
                                                    const cxpr_context* ctx,
                                                    const cxpr_registry* reg,
                                                    const cxpr_value* args,
                                                    size_t argc, cxpr_error* err) {
    double locals[32];
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

static cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                                           const cxpr_registry* reg, const double* locals,
                                           size_t local_count, cxpr_error* err) {
    cxpr_value stack[64];
    size_t sp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        return cxpr_ir_runtime_error(err, "Empty IR program");
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        cxpr_value a, b, result;
        cxpr_value typed_args[32];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(instr->value), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_PUSH_BOOL:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(instr->value != 0.0), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(locals[instr->index]), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            if (!cxpr_ir_push_squared(stack, &sp, cxpr_fv_double(locals[instr->index]), err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_VAR:
            {
                bool found = false;
                result = cxpr_context_get_typed(ctx, instr->name, &found);
                if (!found) {
                    result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                        ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL,
                        false, &found));
                }
                if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier");
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_VAR_SQUARE:
            {
                bool found = false;
                result = cxpr_context_get_typed(ctx, instr->name, &found);
                if (!found) {
                    result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                        ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL,
                        false, &found));
                }
                if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier");
            }
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_PARAM:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable");
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_PARAM_SQUARE:
            {
                bool found = false;
                result = cxpr_fv_double(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable");
            }
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_FIELD:
            result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_FIELD_SQUARE:
            result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_CHAIN:
            result = cxpr_ir_load_chain_value(ctx, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_DIV:
        case CXPR_OP_MOD:
        case CXPR_OP_POW:
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_fv_double(NAN);
            switch (instr->op) {
            case CXPR_OP_ADD:
            case CXPR_OP_SUB:
            case CXPR_OP_MUL:
            case CXPR_OP_DIV:
            case CXPR_OP_MOD:
            case CXPR_OP_POW:
                if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                          "Arithmetic requires double operands") ||
                    !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                          "Arithmetic requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                if (instr->op == CXPR_OP_DIV && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return cxpr_fv_double(NAN);
                }
                if (instr->op == CXPR_OP_MOD && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return cxpr_fv_double(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_ADD: result = cxpr_fv_double(a.d + b.d); break;
                case CXPR_OP_SUB: result = cxpr_fv_double(a.d - b.d); break;
                case CXPR_OP_MUL: result = cxpr_fv_double(a.d * b.d); break;
                case CXPR_OP_DIV: result = cxpr_fv_double(a.d / b.d); break;
                case CXPR_OP_MOD: result = cxpr_fv_double(fmod(a.d, b.d)); break;
                default: result = cxpr_fv_double(pow(a.d, b.d)); break;
                }
                break;
            case CXPR_OP_CMP_EQ:
            case CXPR_OP_CMP_NEQ:
                if (a.type != b.type ||
                    (a.type != CXPR_VALUE_NUMBER && a.type != CXPR_VALUE_BOOL)) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message = "Equality requires matching double/bool operands";
                    }
                    return cxpr_fv_double(NAN);
                }
                if (a.type == CXPR_VALUE_NUMBER) {
                    result = cxpr_fv_bool(instr->op == CXPR_OP_CMP_EQ ? (a.d == b.d) : (a.d != b.d));
                } else {
                    result = cxpr_fv_bool(instr->op == CXPR_OP_CMP_EQ ? (a.b == b.b) : (a.b != b.b));
                }
                break;
            default:
                if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                          "Comparison requires double operands") ||
                    !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                          "Comparison requires double operands")) {
                    return cxpr_fv_double(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_CMP_LT: result = cxpr_fv_bool(a.d < b.d); break;
                case CXPR_OP_CMP_LTE: result = cxpr_fv_bool(a.d <= b.d); break;
                case CXPR_OP_CMP_GT: result = cxpr_fv_bool(a.d > b.d); break;
                default: result = cxpr_fv_bool(a.d >= b.d); break;
                }
                break;
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_SQUARE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Square operation requires double operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(a.d * a.d), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_NOT:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_BOOL, err,
                                      "Logical not requires bool operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(!a.b), 64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_NEG:
        case CXPR_OP_SIGN:
        case CXPR_OP_SQRT:
        case CXPR_OP_ABS:
        case CXPR_OP_FLOOR:
        case CXPR_OP_CEIL:
        case CXPR_OP_ROUND:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Numeric intrinsic requires double operand")) {
                return cxpr_fv_double(NAN);
            }
            switch (instr->op) {
            case CXPR_OP_NEG: result = cxpr_fv_double(-a.d); break;
            case CXPR_OP_SIGN: result = cxpr_fv_double((a.d > 0.0) - (a.d < 0.0)); break;
            case CXPR_OP_SQRT: result = cxpr_fv_double(sqrt(a.d)); break;
            case CXPR_OP_ABS: result = cxpr_fv_double(fabs(a.d)); break;
            case CXPR_OP_FLOOR: result = cxpr_fv_double(floor(a.d)); break;
            case CXPR_OP_CEIL: result = cxpr_fv_double(ceil(a.d)); break;
            default: result = cxpr_fv_double(round(a.d)); break;
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CLAMP:
            if (!cxpr_ir_pop2(stack, &sp, &result, &b, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(result, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands")) {
                return cxpr_fv_double(NAN);
            }
            if (a.d < result.d) a.d = result.d;
            if (a.d > b.d) a.d = b.d;
            if (!cxpr_ir_stack_push(stack, &sp, a, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_UNARY:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->native_scalar.unary(a.d)),
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_BINARY:
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->native_scalar.binary(a.d, b.d)),
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_TERNARY:
            if (!cxpr_ir_pop2(stack, &sp, &b, &result, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(result, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(
                    stack, &sp,
                    cxpr_fv_double(instr->func->native_scalar.ternary(a.d, b.d, result.d)),
                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if (instr->index > 32) return cxpr_ir_runtime_error(err, "Too many function arguments");
            for (size_t i = 0; i < instr->index; ++i) {
                typed_args[i] = stack[sp - instr->index + i];
            }
            sp -= instr->index;
            result = cxpr_registry_call_typed(reg, instr->func->name, typed_args, instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result,
                                    64, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_DEFINED:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
                                                 &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_PRODUCER:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if ((ip + 1) < program->count && program->code[ip + 1].op == CXPR_OP_GET_FIELD) {
                result = cxpr_ir_call_producer_field((cxpr_func_entry*)instr->func, instr->name, ctx,
                                                     &stack[sp - instr->index], instr->index,
                                                     program->code[ip + 1].name, err);
                if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
                sp -= instr->index;
                if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer((cxpr_func_entry*)instr->func, instr->name, ctx,
                                           &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_PRODUCER_CONST:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if ((ip + 1) < program->count && program->code[ip + 1].op == CXPR_OP_GET_FIELD) {
                result = cxpr_ir_call_producer_field_cached((cxpr_func_entry*)instr->func,
                                                            instr->func->name, instr->name, ctx,
                                                            &stack[sp - instr->index], instr->index,
                                                            program->code[ip + 1].name, err);
                if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
                sp -= instr->index;
                if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer_cached((cxpr_func_entry*)instr->func,
                                                  instr->func->name, instr->name, ctx,
                                                  &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
            result = cxpr_ir_call_producer_const_field((cxpr_func_entry*)instr->func,
                                                       instr->name, ctx,
                                                       (const double*)instr->payload,
                                                       instr->index, instr->aux_name, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_GET_FIELD:
            {
                bool found = false;

                if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
                if (!cxpr_ir_require_type(a, CXPR_VALUE_STRUCT, err,
                                          "Field access requires struct operand")) {
                    return cxpr_fv_double(NAN);
                }
                result = cxpr_ir_struct_get_field(a.s, instr->name, &found);
                if (!found) return cxpr_ir_make_not_found(err, "Unknown field access");
                if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_AST:
            (void)cxpr_eval_ast(instr->ast, ctx, reg, &result, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, 64, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
        case CXPR_OP_JUMP_IF_TRUE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_BOOL, err,
                                      "Conditional jump requires bool operand")) {
                return cxpr_fv_double(NAN);
            }
            if ((instr->op == CXPR_OP_JUMP_IF_FALSE && !a.b) ||
                (instr->op == CXPR_OP_JUMP_IF_TRUE && a.b)) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            if (!cxpr_ir_pop1(stack, &sp, &result, err)) return cxpr_fv_double(NAN);
            if (sp != 0) {
                return cxpr_ir_runtime_error(err, "IR stack not empty at return");
            }
            return result;
        default:
            return cxpr_ir_runtime_error(err, "Unsupported IR opcode");
        }

        ++ip;
    }

    return cxpr_ir_runtime_error(err, "IR program fell off end without return");
}

static double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                       const cxpr_registry* reg, const double* locals,
                                       size_t local_count, cxpr_error* err) {
    double stack[CXPR_IR_STACK_CAPACITY];
    size_t sp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return NAN;
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        double a, b, value;
        double args[32];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            stack[sp++] = instr->value;
            break;
        case CXPR_OP_PUSH_BOOL:
            stack[sp++] = instr->value != 0.0 ? 1.0 : 0.0;
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable").d;
            }
            stack[sp++] = locals[instr->index];
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable").d;
            }
            value = locals[instr->index];
            stack[sp++] = value * value;
            break;
        case CXPR_OP_LOAD_VAR: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_VAR_SQUARE: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value * value;
            break;
        }
        case CXPR_OP_LOAD_PARAM: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            bool found = false;
            value = cxpr_ir_lookup_cached_scalar(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable").d;
            stack[sp++] = value * value;
            break;
        }
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_DIV:
        case CXPR_OP_MOD:
        case CXPR_OP_POW:
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            b = stack[--sp];
            a = stack[--sp];
            switch (instr->op) {
            case CXPR_OP_ADD: value = a + b; break;
            case CXPR_OP_SUB: value = a - b; break;
            case CXPR_OP_MUL: value = a * b; break;
            case CXPR_OP_DIV:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return NAN;
                }
                value = a / b;
                break;
            case CXPR_OP_MOD:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return NAN;
                }
                value = fmod(a, b);
                break;
            case CXPR_OP_POW: value = pow(a, b); break;
            case CXPR_OP_CMP_EQ: value = (a == b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_NEQ: value = (a != b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_LT: value = (a < b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_LTE: value = (a <= b) ? 1.0 : 0.0; break;
            case CXPR_OP_CMP_GT: value = (a > b) ? 1.0 : 0.0; break;
            default: value = (a >= b) ? 1.0 : 0.0; break;
            }
            stack[sp++] = value;
            break;
        case CXPR_OP_SQUARE:
            stack[sp - 1] *= stack[sp - 1];
            break;
        case CXPR_OP_NOT:
            stack[sp - 1] = stack[sp - 1] == 0.0 ? 1.0 : 0.0;
            break;
        case CXPR_OP_NEG:
            stack[sp - 1] = -stack[sp - 1];
            break;
        case CXPR_OP_SIGN:
            value = stack[sp - 1];
            stack[sp - 1] = (value > 0.0) - (value < 0.0);
            break;
        case CXPR_OP_SQRT:
            stack[sp - 1] = sqrt(stack[sp - 1]);
            break;
        case CXPR_OP_ABS:
            stack[sp - 1] = fabs(stack[sp - 1]);
            break;
        case CXPR_OP_FLOOR:
            stack[sp - 1] = floor(stack[sp - 1]);
            break;
        case CXPR_OP_CEIL:
            stack[sp - 1] = ceil(stack[sp - 1]);
            break;
        case CXPR_OP_ROUND:
            stack[sp - 1] = round(stack[sp - 1]);
            break;
        case CXPR_OP_CLAMP:
            b = stack[--sp];
            a = stack[--sp];
            value = stack[--sp];
            if (value < a) value = a;
            if (value > b) value = b;
            stack[sp++] = value;
            break;
        case CXPR_OP_CALL_UNARY:
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.unary(a);
            break;
        case CXPR_OP_CALL_BINARY:
            b = stack[--sp];
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.binary(a, b);
            break;
        case CXPR_OP_CALL_TERNARY:
            value = stack[--sp];
            b = stack[--sp];
            a = stack[--sp];
            stack[sp++] = instr->func->native_scalar.ternary(a, b, value);
            break;
        case CXPR_OP_CALL_FUNC:
            if (instr->func->typed_func) {
                return cxpr_ir_runtime_error(err, "Typed function requires typed execution path").d;
            }
            for (size_t i = 0; i < instr->index; ++i) {
                args[i] = stack[sp - instr->index + i];
            }
            sp -= instr->index;
            if (instr->func->value_func) {
                cxpr_value value_result = instr->func->value_func(args, instr->index, instr->func->userdata);
                if (value_result.type == CXPR_VALUE_NUMBER) stack[sp++] = value_result.d;
                else if (value_result.type == CXPR_VALUE_BOOL) stack[sp++] = value_result.b ? 1.0 : 0.0;
                else return cxpr_ir_runtime_error(err, "Function did not evaluate to scalar").d;
            } else {
                stack[sp++] = instr->func->sync_func(args, instr->index, instr->func->userdata);
            }
            break;
        case CXPR_OP_CALL_DEFINED: {
            cxpr_value result;
            cxpr_value scalar_args[32];
            for (size_t i = 0; i < instr->index; ++i) {
                scalar_args[i] = cxpr_fv_double(stack[sp - instr->index + i]);
            }
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
                                                 scalar_args, instr->index, err);
            if (err && err->code != CXPR_OK) return NAN;
            sp -= instr->index;
            if (result.type == CXPR_VALUE_NUMBER) value = result.d;
            else if (result.type == CXPR_VALUE_BOOL) value = result.b ? 1.0 : 0.0;
            else {
                if (err) {
                    err->code = CXPR_ERR_TYPE_MISMATCH;
                    err->message = "Defined function returned non-scalar";
                }
                return NAN;
            }
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
            if (stack[--sp] == 0.0) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            if (stack[--sp] != 0.0) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            if (sp != 1) return cxpr_ir_runtime_error(err, "IR stack imbalance on return").d;
            value = stack[--sp];
            return value;
        default:
            return cxpr_ir_runtime_error(err, "Unsupported fast IR opcode").d;
        }

        ++ip;
    }

    return cxpr_ir_runtime_error(err, "IR program fell off end without return").d;
}

bool cxpr_ir_prepare_defined_program(cxpr_func_entry* entry, const cxpr_registry* reg,
                                     cxpr_error* err) {
    if (!entry || !entry->defined_body || !cxpr_ir_defined_is_scalar_only(entry)) {
        return false;
    }
    if (entry->defined_program || entry->defined_program_failed) {
        return entry->defined_program != NULL;
    }
    if (err) *err = (cxpr_error){0};

    entry->defined_program = (cxpr_program*)calloc(1, sizeof(cxpr_program));
    if (!entry->defined_program) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }

    if (!cxpr_ir_compile_with_locals(entry->defined_body, reg,
                                     (const char* const*)entry->defined_param_names,
                                     entry->defined_param_count, &entry->defined_program->ir,
                                     err)) {
        entry->defined_program_failed = true;
        cxpr_program_free(entry->defined_program);
        entry->defined_program = NULL;
        return false;
    }

    entry->defined_program->ast = entry->defined_body;
    return true;
}

double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, locals, local_count, err);
    }
    cxpr_value value = cxpr_ir_exec_typed(program, ctx, reg, locals, local_count, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return NAN;
    }
    return value.d;
}

double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err) {
    if (program && program->fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        return cxpr_ir_exec_scalar_fast(program, ctx, reg, NULL, 0, err);
    }
    cxpr_value value = cxpr_ir_exec_typed(program, ctx, reg, NULL, 0, err);
    if (err && err->code != CXPR_OK) return NAN;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return NAN;
    }
    return value.d;
}

static cxpr_value cxpr_eval_program_value(const cxpr_program* prog, const cxpr_context* ctx,
                                          const cxpr_registry* reg, cxpr_error* err) {
    if (!prog) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL compiled program";
        }
        return cxpr_fv_double(NAN);
    }
    return cxpr_ir_exec_typed(&prog->ir, ctx, reg, NULL, 0, err);
}

bool cxpr_eval_program(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    *out_value = value;
    return true;
}

bool cxpr_eval_program_number(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, double* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = fast_value;
        return true;
    }
    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_DOUBLE) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = fast_value;
        return true;
    }
    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_NUMBER) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to double";
        }
        return false;
    }
    *out_value = value.d;
    return true;
}

bool cxpr_eval_program_bool(const cxpr_program* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, bool* out_value, cxpr_error* err) {
    cxpr_value value;

    if (!out_value) {
        if (err) {
            *err = (cxpr_error){0};
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }

    if (prog && prog->ir.fast_result_kind == CXPR_IR_RESULT_BOOL) {
        double fast_value = cxpr_ir_exec_scalar_fast(&prog->ir, ctx, reg, NULL, 0, err);
        if (err && err->code != CXPR_OK) return false;
        *out_value = (fast_value != 0.0);
        return true;
    }
    value = cxpr_eval_program_value(prog, ctx, reg, err);
    if (err && err->code != CXPR_OK) return false;
    if (value.type != CXPR_VALUE_BOOL) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Expression did not evaluate to bool";
        }
        return false;
    }
    *out_value = value.b;
    return true;
}
