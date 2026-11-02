/**
 * @file fast.c
 * @brief Fast scalar IR executor.
 */

#include "internal.h"
#include <math.h>
#include <stdio.h>

#if !defined(__GNUC__) && !defined(__clang__)
#error "cxpr fast IR executor requires GCC/Clang computed goto support"
#endif

/*
 * This executor is built on computed goto (the "labels as values" extension):
 * a jump table of `&&label` addresses dispatched with `goto *target`. That is a
 * deliberate, GCC/Clang-only design choice for a tight bytecode loop, but it is
 * not ISO C, so -Wpedantic flags it. Silence that one diagnostic for this file;
 * all other strict warnings remain in force.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

static const char* cxpr_ir_unknown_param_message(const cxpr_ir_instr* instr) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!instr || !instr->name || instr->name[0] == '\0') {
        return "Unknown parameter variable";
    }
    snprintf(message, sizeof(message), "Unknown parameter variable '%s'", instr->name);
    return message;
}

static const char* cxpr_ir_unknown_identifier_message(const cxpr_ir_instr* instr) {
    static CXPR_THREAD_LOCAL char message[256];
    if (!instr || !instr->name || instr->name[0] == '\0') {
        return "Unknown identifier";
    }
    snprintf(message, sizeof(message), "Unknown identifier '%s'", instr->name);
    return message;
}

static inline double cxpr_ir_exec_lookup_scalar_fast(const cxpr_context* ctx,
                                                     const cxpr_ir_instr* instr,
                                                     cxpr_ir_lookup_cache* cache,
                                                     bool param_lookup,
                                                     bool* found) {
    const cxpr_hashmap* map;
    const cxpr_hashmap_entry* entry;
    unsigned long version;

    if (ctx && !ctx->parent && !ctx->expression_scope &&
        (param_lookup || ctx->bools.count == 0u)) {
        map = param_lookup ? &ctx->params : &ctx->variables;
        version = param_lookup ? ctx->params_version : ctx->variables_version;

        if (cache && cache->request_ctx == ctx && cache->owner_ctx == ctx &&
            cache->entries_base == map->entries && cache->shadow_version == version) {
            *found = true;
            return cache->entries_base[cache->slot].value;
        }

        entry = cxpr_hashmap_find_prehashed_entry(map, instr->name, instr->hash);
        if (entry) {
            if (cache) {
                cache->request_ctx = ctx;
                cache->owner_ctx = ctx;
                cache->entries_base = map->entries;
                cache->slot = (size_t)(entry - map->entries);
                cache->shadow_version = version;
            }
            *found = true;
            return entry->value;
        }

        if (cache) {
            cache->request_ctx = NULL;
            cache->owner_ctx = NULL;
            cache->entries_base = NULL;
            cache->slot = 0;
            cache->shadow_version = 0;
        }
        *found = false;
        return 0.0;
    }

    return cxpr_ir_lookup_cached_scalar(ctx, instr, cache, param_lookup, found);
}

static inline bool cxpr_ir_exec_lookup_root_cached(const cxpr_context* ctx,
                                                  const cxpr_hashmap* map,
                                                  unsigned long version,
                                                  const cxpr_ir_instr* instr,
                                                  cxpr_ir_lookup_cache* cache,
                                                  double* out) {
    const cxpr_hashmap_entry* entry;

    if (cache && cache->request_ctx == ctx && cache->entries_base == map->entries &&
        cache->shadow_version == version) {
        *out = cache->entries_base[cache->slot].value;
        return true;
    }

    entry = cxpr_hashmap_find_prehashed_entry(map, instr->name, instr->hash);
    if (!entry) {
        if (cache) {
            cache->request_ctx = NULL;
            cache->owner_ctx = NULL;
            cache->entries_base = NULL;
            cache->slot = 0;
            cache->shadow_version = 0;
        }
        return false;
    }

    if (cache) {
        cache->request_ctx = ctx;
        cache->owner_ctx = ctx;
        cache->entries_base = map->entries;
        cache->slot = (size_t)(entry - map->entries);
        cache->shadow_version = version;
    }
    *out = entry->value;
    return true;
}

static inline bool cxpr_ir_exec_column_lookback_fast(const cxpr_ir_instr* instr,
                                                     size_t offset,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     double* out) {
    const cxpr_ir_lookback_payload* payload;
    const cxpr_context* current;
    int64_t index;

    if (!instr || !out || offset > (size_t)INT64_MAX) return false;
    payload = (const cxpr_ir_lookback_payload*)instr->payload;
    if (!payload || !payload->column_bound || reg != payload->compile_registry ||
        reg->lookback_resolver != payload->resolver ||
        reg->lookback_userdata != payload->resolver_userdata) {
        return false;
    }
    for (current = ctx; current; current = current->parent) {
        if (current->arrays.count != 0u) return false;
    }
    index = (payload->column.cursor ? *payload->column.cursor : 0) - (int64_t)offset;
    if (!payload->column.base || index < 0 || (size_t)index >= payload->column.count) {
        *out = NAN;
        return true;
    }
    *out = *(const double*)((const char*)payload->column.base +
                           (size_t)index * payload->column.stride);
    return true;
}

double cxpr_ir_exec_scalar_fast_bound(const cxpr_ir_program* program,
                                      const cxpr_context* ctx, const cxpr_registry* reg,
                                      double* locals, size_t local_count,
                                      cxpr_ir_bindings* bindings, cxpr_error* err) {
    double stack[CXPR_IR_STACK_CAPACITY];
    /* Compiler temporaries are definition-dominated: every LOAD_TEMP follows
       its STORE_TEMP. Avoid clearing the full scratch array on every scalar
       evaluation, including programs whose temp_count is zero. */
    double temps[CXPR_IR_STACK_CAPACITY];
    size_t sp = 0;
    size_t ip = 0;
    const bool fast_var_ctx = ctx && !ctx->parent && !ctx->expression_scope &&
                              ctx->bools.count == 0u;
    const bool fast_param_ctx = ctx && !ctx->parent && !ctx->expression_scope;
    const cxpr_hashmap* fast_var_map = fast_var_ctx ? &ctx->variables : NULL;
    const cxpr_hashmap* fast_param_map = fast_param_ctx ? &ctx->params : NULL;
    const unsigned long fast_var_version = fast_var_ctx ? ctx->variables_version : 0u;
    const unsigned long fast_param_version = fast_param_ctx ? ctx->params_version : 0u;
    const double* const* direct_values;
    cxpr_ir_lookup_cache* lookup_cache;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return NAN;
    }
    cxpr_ir_bindings_refresh(program, bindings, ctx);
    direct_values = bindings && bindings->direct_ready ? bindings->direct_values : NULL;
    lookup_cache = bindings ? bindings->inputs : NULL;

    static void* dispatch[] = {
        [CXPR_OP_PUSH_CONST] = &&op_push_const,
        [CXPR_OP_PUSH_INT64] = &&op_push_int64,
        [CXPR_OP_PUSH_BOOL] = &&op_push_bool,
        [CXPR_OP_PUSH_STRING] = &&op_unsupported,
        [CXPR_OP_LOAD_LOCAL] = &&op_load_local,
        [CXPR_OP_LOAD_LOCAL_SQUARE] = &&op_load_local_square,
        [CXPR_OP_LOAD_VAR] = &&op_load_var,
        [CXPR_OP_LOAD_VAR_SQUARE] = &&op_load_var_square,
        [CXPR_OP_LOAD_PARAM] = &&op_load_param,
        [CXPR_OP_LOAD_PARAM_SQUARE] = &&op_load_param_square,
        [CXPR_OP_LOAD_FIELD] = &&op_unsupported,
        [CXPR_OP_LOAD_FIELD_SQUARE] = &&op_unsupported,
        [CXPR_OP_LOAD_NAMED_FIELD] = &&op_load_named_field,
        [CXPR_OP_LOAD_CHAIN] = &&op_unsupported,
        [CXPR_OP_ADD] = &&op_add,
        [CXPR_OP_SUB] = &&op_sub,
        [CXPR_OP_MUL] = &&op_mul,
        [CXPR_OP_SQUARE] = &&op_square,
        [CXPR_OP_DIV] = &&op_div,
        [CXPR_OP_MOD] = &&op_mod,
        [CXPR_OP_CMP_EQ] = &&op_cmp_eq,
        [CXPR_OP_CMP_NEQ] = &&op_cmp_neq,
        [CXPR_OP_CMP_LT] = &&op_cmp_lt,
        [CXPR_OP_CMP_LTE] = &&op_cmp_lte,
        [CXPR_OP_CMP_GT] = &&op_cmp_gt,
        [CXPR_OP_CMP_GTE] = &&op_cmp_gte,
        [CXPR_OP_NOT] = &&op_not,
        [CXPR_OP_NEG] = &&op_neg,
        [CXPR_OP_SIGN] = &&op_sign,
        [CXPR_OP_SQRT] = &&op_sqrt,
        [CXPR_OP_ABS] = &&op_abs,
        [CXPR_OP_FLOOR] = &&op_floor,
        [CXPR_OP_CEIL] = &&op_ceil,
        [CXPR_OP_ROUND] = &&op_round,
        [CXPR_OP_POW] = &&op_pow,
        [CXPR_OP_CLAMP] = &&op_clamp,
        [CXPR_OP_CALL_PRODUCER] = &&op_unsupported,
        [CXPR_OP_CALL_PRODUCER_CONST] = &&op_unsupported,
        [CXPR_OP_CALL_PRODUCER_CONST_FIELD] = &&op_unsupported,
        [CXPR_OP_GET_FIELD] = &&op_unsupported,
        [CXPR_OP_CALL_UNARY] = &&op_call_unary,
        [CXPR_OP_CALL_BINARY] = &&op_call_binary,
        [CXPR_OP_CALL_TERNARY] = &&op_call_ternary,
        [CXPR_OP_CALL_FUNC] = &&op_call_func,
        [CXPR_OP_CALL_DEFINED] = &&op_call_defined,
        [CXPR_OP_CALL_AST] = &&op_unsupported,
        [CXPR_OP_JUMP] = &&op_jump,
        [CXPR_OP_JUMP_IF_FALSE] = &&op_jump_if_false,
        [CXPR_OP_JUMP_IF_TRUE] = &&op_jump_if_true,
        [CXPR_OP_LOOKBACK_PUSH] = &&op_unsupported,
        [CXPR_OP_LOOKBACK_POP] = &&op_unsupported,
        [CXPR_OP_LOOKBACK_RESOLVE] = &&op_lookback_resolve,
        [CXPR_OP_STORE_LOCAL] = &&op_store_local,
        [CXPR_OP_LOAD_TEMP] = &&op_load_temp,
        [CXPR_OP_STORE_TEMP] = &&op_store_temp,
        [CXPR_OP_RETURN] = &&op_return,
        [CXPR_OP_INDEX] = &&op_unsupported
    };
    const cxpr_ir_instr* instr;
    double a, b, value;
    cxpr_value memo_value;
    cxpr_value scalar_args[CXPR_MAX_CALL_ARGS];

#define CXPR_FAST_DISPATCH()                                                        \
    do {                                                                            \
        if (ip >= program->count) {                                                 \
            return cxpr_ir_runtime_error(err, "IR program fell off end without return").d; \
        }                                                                           \
        instr = &program->code[ip];                                                 \
        if ((unsigned)instr->op > (unsigned)CXPR_OP_INDEX) {                        \
            goto op_unsupported;                                                    \
        }                                                                           \
        goto *dispatch[instr->op];                                                  \
    } while (0)
#define CXPR_FAST_NEXT()                                                            \
    do {                                                                            \
        ++ip;                                                                       \
        CXPR_FAST_DISPATCH();                                                       \
    } while (0)

    CXPR_FAST_DISPATCH();

op_push_const:
    stack[sp++] = instr->value;
    CXPR_FAST_NEXT();
op_push_int64:
    stack[sp++] = (double)instr->int64_value;
    CXPR_FAST_NEXT();
op_push_bool:
    stack[sp++] = instr->value != 0.0 ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_load_local:
    if (instr->index >= local_count) return cxpr_ir_runtime_error(err, "Unknown local variable").d;
    stack[sp++] = locals[instr->index];
    CXPR_FAST_NEXT();
op_load_temp:
    if (instr->index >= program->temp_count)
        return cxpr_ir_runtime_error(err, "Unknown compiler temporary").d;
    stack[sp++] = temps[instr->index];
    CXPR_FAST_NEXT();
op_load_local_square:
    if (instr->index >= local_count) return cxpr_ir_runtime_error(err, "Unknown local variable").d;
    value = locals[instr->index];
    stack[sp++] = value * value;
    CXPR_FAST_NEXT();
op_load_var: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_var_map) {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_var_map, fast_var_version, instr,
                                             cache, &value)) {
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr)).d;
        }
    } else {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, false, &found);
        if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr)).d;
    }
    stack[sp++] = value;
    CXPR_FAST_NEXT();
}
op_load_var_square: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_var_map) {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_var_map, fast_var_version, instr,
                                             cache, &value)) {
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr)).d;
        }
    } else {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, false, &found);
        if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr)).d;
    }
    stack[sp++] = value * value;
    CXPR_FAST_NEXT();
}
op_load_named_field: {
    cxpr_value field_value;
    if (cxpr_ir_bindings_direct_named_number(program, bindings, ip, &value)) {
        field_value = cxpr_num(value);
    } else if (cxpr_ir_bindings_direct_typed_value(program, bindings, ip, &field_value)) {
    } else {
        field_value = cxpr_ir_load_named_field_value(ctx, instr, err);
    }
    if (err && err->code != CXPR_OK) return NAN;
    if (field_value.type != CXPR_VALUE_NUMBER) {
        cxpr_value_free(&field_value);
        return cxpr_ir_runtime_error(err, "Struct field did not evaluate to double").d;
    }
    value = field_value.d;
    cxpr_value_free(&field_value);
    stack[sp++] = value;
    CXPR_FAST_NEXT();
}
op_load_param: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_param_map) {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_param_map, fast_param_version, instr,
                                             cache, &value)) {
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr)).d;
        }
    } else {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, true, &found);
        if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr)).d;
    }
    stack[sp++] = value;
    CXPR_FAST_NEXT();
}
op_load_param_square: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_param_map) {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_param_map, fast_param_version, instr,
                                             cache, &value)) {
            return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr)).d;
        }
    } else {
        cxpr_ir_lookup_cache* cache = lookup_cache ? &lookup_cache[ip] : NULL;
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, true, &found);
        if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr)).d;
    }
    stack[sp++] = value * value;
    CXPR_FAST_NEXT();
}
op_lookback_resolve: {
    cxpr_value resolved = cxpr_num(NAN);
    if (cxpr_ir_exec_column_lookback_fast(instr, instr->index, ctx, reg, &value)) {
        stack[sp++] = value;
        CXPR_FAST_NEXT();
    }
    if (!cxpr_ir_resolve_lookback_instr(instr, instr->index, ctx, reg, &resolved, err)) {
        if (err && err->code != CXPR_OK) return NAN;
        return cxpr_ir_runtime_error(err, "Lookback resolver failed").d;
    }
    if (resolved.type != CXPR_VALUE_NUMBER) {
        if (resolved.type == CXPR_VALUE_ARRAY || resolved.type == CXPR_VALUE_STRUCT) {
            cxpr_value_free(&resolved);
        }
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Lookback did not evaluate to double";
        }
        return NAN;
    }
    stack[sp++] = resolved.d;
    CXPR_FAST_NEXT();
}
op_add:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = a + b;
    CXPR_FAST_NEXT();
op_sub:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = a - b;
    CXPR_FAST_NEXT();
op_mul:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = a * b;
    CXPR_FAST_NEXT();
op_div:
    b = stack[--sp];
    a = stack[--sp];
    if (b == 0.0) {
        if (err) {
            err->code = CXPR_ERR_DIVISION_BY_ZERO;
            err->message = "Division by zero";
        }
        return NAN;
    }
    stack[sp++] = a / b;
    CXPR_FAST_NEXT();
op_mod:
    b = stack[--sp];
    a = stack[--sp];
    if (b == 0.0) {
        if (err) {
            err->code = CXPR_ERR_DIVISION_BY_ZERO;
            err->message = "Modulo by zero";
        }
        return NAN;
    }
    stack[sp++] = fmod(a, b);
    CXPR_FAST_NEXT();
op_pow:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = pow(a, b);
    CXPR_FAST_NEXT();
op_cmp_eq:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a == b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_cmp_neq:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a != b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_cmp_lt:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a < b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_cmp_lte:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a <= b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_cmp_gt:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a > b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_cmp_gte:
    b = stack[--sp];
    a = stack[--sp];
    stack[sp++] = (a >= b) ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_square:
    stack[sp - 1] *= stack[sp - 1];
    CXPR_FAST_NEXT();
op_not:
    stack[sp - 1] = stack[sp - 1] == 0.0 ? 1.0 : 0.0;
    CXPR_FAST_NEXT();
op_neg:
    stack[sp - 1] = -stack[sp - 1];
    CXPR_FAST_NEXT();
op_sign:
    value = stack[sp - 1];
    stack[sp - 1] = (value > 0.0) - (value < 0.0);
    CXPR_FAST_NEXT();
op_sqrt:
    stack[sp - 1] = sqrt(stack[sp - 1]);
    CXPR_FAST_NEXT();
op_abs:
    stack[sp - 1] = fabs(stack[sp - 1]);
    CXPR_FAST_NEXT();
op_floor:
    stack[sp - 1] = floor(stack[sp - 1]);
    CXPR_FAST_NEXT();
op_ceil:
    stack[sp - 1] = ceil(stack[sp - 1]);
    CXPR_FAST_NEXT();
op_round:
    stack[sp - 1] = round(stack[sp - 1]);
    CXPR_FAST_NEXT();
op_clamp:
    b = stack[--sp];
    a = stack[--sp];
    value = stack[--sp];
    if (value < a) value = a;
    if (value > b) value = b;
    stack[sp++] = value;
    CXPR_FAST_NEXT();
op_call_unary:
    a = stack[--sp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_NUMBER) stack[sp++] = memo_value.d;
        else if (memo_value.type == CXPR_VALUE_BOOL) stack[sp++] = memo_value.b ? 1.0 : 0.0;
        else return cxpr_ir_runtime_error(err, "Memoized function result is not scalar").d;
        CXPR_FAST_NEXT();
    }
    value = instr->func->native_scalar.unary(a);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    stack[sp++] = value;
    CXPR_FAST_NEXT();
op_call_binary:
    b = stack[--sp];
    a = stack[--sp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_NUMBER) stack[sp++] = memo_value.d;
        else if (memo_value.type == CXPR_VALUE_BOOL) stack[sp++] = memo_value.b ? 1.0 : 0.0;
        else return cxpr_ir_runtime_error(err, "Memoized function result is not scalar").d;
        CXPR_FAST_NEXT();
    }
    value = instr->func->native_scalar.binary(a, b);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    stack[sp++] = value;
    CXPR_FAST_NEXT();
op_call_ternary:
    value = stack[--sp];
    b = stack[--sp];
    a = stack[--sp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_NUMBER) stack[sp++] = memo_value.d;
        else if (memo_value.type == CXPR_VALUE_BOOL) stack[sp++] = memo_value.b ? 1.0 : 0.0;
        else return cxpr_ir_runtime_error(err, "Memoized function result is not scalar").d;
        CXPR_FAST_NEXT();
    }
    value = instr->func->native_scalar.ternary(a, b, value);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    stack[sp++] = value;
    CXPR_FAST_NEXT();
op_call_func:
    if (instr->func->typed_func) {
        return cxpr_ir_runtime_error(err, "Typed function requires typed execution path").d;
    }
    sp -= instr->index;
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_NUMBER) stack[sp++] = memo_value.d;
        else if (memo_value.type == CXPR_VALUE_BOOL) stack[sp++] = memo_value.b ? 1.0 : 0.0;
        else return cxpr_ir_runtime_error(err, "Memoized function result is not scalar").d;
        CXPR_FAST_NEXT();
    }
    if (instr->func->value_func) {
        for (size_t i = 0; i < instr->index; ++i) {
            scalar_args[i] = cxpr_num(stack[sp + i]);
        }
        cxpr_value value_result = instr->func->value_func(
            scalar_args, instr->index, instr->func->userdata);
        if (value_result.type == CXPR_VALUE_NUMBER) stack[sp++] = value_result.d;
        else if (value_result.type == CXPR_VALUE_BOOL) stack[sp++] = value_result.b ? 1.0 : 0.0;
        else return cxpr_ir_runtime_error(err, "Function did not evaluate to scalar").d;
        (void)cxpr_ir_call_memo_set(ctx, instr, value_result);
    } else {
        double call_result = instr->func->sync_func(&stack[sp], instr->index, instr->func->userdata);
        (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(call_result));
        stack[sp++] = call_result;
    }
    CXPR_FAST_NEXT();
op_call_defined: {
    cxpr_value result;
    for (size_t i = 0; i < instr->index; ++i) {
        scalar_args[i] = cxpr_num(stack[sp - instr->index + i]);
    }
    result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, instr->payload, ctx, reg,
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
    CXPR_FAST_NEXT();
}
op_jump:
    ip = instr->index;
    CXPR_FAST_DISPATCH();
op_jump_if_false:
    if (stack[--sp] == 0.0) {
        ip = instr->index;
        CXPR_FAST_DISPATCH();
    }
    CXPR_FAST_NEXT();
op_jump_if_true:
    if (stack[--sp] != 0.0) {
        ip = instr->index;
        CXPR_FAST_DISPATCH();
    }
    CXPR_FAST_NEXT();
op_store_local:
    if (instr->index >= local_count) return cxpr_ir_runtime_error(err, "Unknown local variable").d;
    locals[instr->index] = stack[--sp];
    CXPR_FAST_NEXT();
op_store_temp:
    if (instr->index >= program->temp_count)
        return cxpr_ir_runtime_error(err, "Unknown compiler temporary").d;
    temps[instr->index] = stack[--sp];
    CXPR_FAST_NEXT();
op_return:
    if (sp != 1) return cxpr_ir_runtime_error(err, "IR stack imbalance on return").d;
    return stack[--sp];
op_unsupported:
    return cxpr_ir_runtime_error(err, "Unsupported fast IR opcode").d;

#undef CXPR_FAST_NEXT
#undef CXPR_FAST_DISPATCH
}

double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, double* locals,
                                size_t local_count, cxpr_error* err) {
    return cxpr_ir_exec_scalar_fast_bound(
        program, ctx, reg, locals, local_count, NULL, err);
}

bool cxpr_ir_exec_bool_fast_bound(const cxpr_ir_program* program,
                                  const cxpr_context* ctx, const cxpr_registry* reg,
                                  const double* locals, size_t local_count,
                                  cxpr_ir_bindings* bindings, bool* out_value,
                                  cxpr_error* err) {
    double nstack[CXPR_IR_STACK_CAPACITY];
    bool bstack[CXPR_IR_STACK_CAPACITY];
    size_t nsp = 0;
    size_t bsp = 0;
    size_t ip = 0;

    if (program && program->code) {
        for (size_t i = 0; i < program->count; ++i) {
            if (program->code[i].op == CXPR_OP_PUSH_INT64) {
                cxpr_value value = cxpr_ir_exec_typed_bound(
                    program, ctx, reg, locals, local_count, bindings, err);
                if (err && err->code != CXPR_OK) return false;
                if (value.type != CXPR_VALUE_BOOL) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message = "Expression did not evaluate to bool";
                    }
                    return false;
                }
                if (out_value) *out_value = value.b;
                return out_value != NULL;
            }
        }
    }
    const bool fast_var_ctx = ctx && !ctx->parent && !ctx->expression_scope &&
                              ctx->bools.count == 0u;
    const bool fast_param_ctx = ctx && !ctx->parent && !ctx->expression_scope;
    const cxpr_hashmap* fast_var_map = fast_var_ctx ? &ctx->variables : NULL;
    const cxpr_hashmap* fast_param_map = fast_param_ctx ? &ctx->params : NULL;
    const unsigned long fast_var_version = fast_var_ctx ? ctx->variables_version : 0u;
    const unsigned long fast_param_version = fast_param_ctx ? ctx->params_version : 0u;
    const double* const* direct_values;

    if (err) *err = (cxpr_error){0};
    if (!out_value) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    cxpr_ir_bindings_refresh(program, bindings, ctx);
    direct_values = bindings && bindings->direct_ready ? bindings->direct_values : NULL;
    if (!program || !program->code) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return false;
    }

    static void* dispatch[] = {
        [CXPR_OP_PUSH_CONST] = &&opb_push_const,
        [CXPR_OP_PUSH_BOOL] = &&opb_push_bool,
        [CXPR_OP_PUSH_STRING] = &&opb_unsupported,
        [CXPR_OP_LOAD_LOCAL] = &&opb_load_local,
        [CXPR_OP_LOAD_LOCAL_SQUARE] = &&opb_load_local_square,
        [CXPR_OP_LOAD_VAR] = &&opb_load_var,
        [CXPR_OP_LOAD_VAR_SQUARE] = &&opb_load_var_square,
        [CXPR_OP_LOAD_PARAM] = &&opb_load_param,
        [CXPR_OP_LOAD_PARAM_SQUARE] = &&opb_load_param_square,
        [CXPR_OP_LOAD_FIELD] = &&opb_unsupported,
        [CXPR_OP_LOAD_FIELD_SQUARE] = &&opb_unsupported,
        [CXPR_OP_LOAD_NAMED_FIELD] = &&opb_unsupported,
        [CXPR_OP_LOAD_CHAIN] = &&opb_unsupported,
        [CXPR_OP_ADD] = &&opb_add,
        [CXPR_OP_SUB] = &&opb_sub,
        [CXPR_OP_MUL] = &&opb_mul,
        [CXPR_OP_SQUARE] = &&opb_square,
        [CXPR_OP_DIV] = &&opb_div,
        [CXPR_OP_MOD] = &&opb_mod,
        [CXPR_OP_CMP_EQ] = &&opb_cmp_eq,
        [CXPR_OP_CMP_NEQ] = &&opb_cmp_neq,
        [CXPR_OP_CMP_LT] = &&opb_cmp_lt,
        [CXPR_OP_CMP_LTE] = &&opb_cmp_lte,
        [CXPR_OP_CMP_GT] = &&opb_cmp_gt,
        [CXPR_OP_CMP_GTE] = &&opb_cmp_gte,
        [CXPR_OP_NOT] = &&opb_not,
        [CXPR_OP_NEG] = &&opb_neg,
        [CXPR_OP_SIGN] = &&opb_sign,
        [CXPR_OP_SQRT] = &&opb_sqrt,
        [CXPR_OP_ABS] = &&opb_abs,
        [CXPR_OP_FLOOR] = &&opb_floor,
        [CXPR_OP_CEIL] = &&opb_ceil,
        [CXPR_OP_ROUND] = &&opb_round,
        [CXPR_OP_POW] = &&opb_pow,
        [CXPR_OP_CLAMP] = &&opb_clamp,
        [CXPR_OP_CALL_PRODUCER] = &&opb_unsupported,
        [CXPR_OP_CALL_PRODUCER_CONST] = &&opb_unsupported,
        [CXPR_OP_CALL_PRODUCER_CONST_FIELD] = &&opb_unsupported,
        [CXPR_OP_GET_FIELD] = &&opb_unsupported,
        [CXPR_OP_CALL_UNARY] = &&opb_call_unary,
        [CXPR_OP_CALL_BINARY] = &&opb_call_binary,
        [CXPR_OP_CALL_TERNARY] = &&opb_call_ternary,
        [CXPR_OP_CALL_FUNC] = &&opb_call_func,
        [CXPR_OP_CALL_DEFINED] = &&opb_call_defined,
        [CXPR_OP_CALL_AST] = &&opb_unsupported,
        [CXPR_OP_JUMP] = &&opb_jump,
        [CXPR_OP_JUMP_IF_FALSE] = &&opb_jump_if_false,
        [CXPR_OP_JUMP_IF_TRUE] = &&opb_jump_if_true,
        [CXPR_OP_LOOKBACK_PUSH] = &&opb_unsupported,
        [CXPR_OP_LOOKBACK_POP] = &&opb_unsupported,
        [CXPR_OP_LOOKBACK_RESOLVE] = &&opb_unsupported,
        [CXPR_OP_STORE_LOCAL] = &&opb_unsupported,
        [CXPR_OP_LOAD_TEMP] = &&opb_unsupported,
        [CXPR_OP_STORE_TEMP] = &&opb_unsupported,
        [CXPR_OP_RETURN] = &&opb_return,
        [CXPR_OP_INDEX] = &&opb_unsupported
    };
    const cxpr_ir_instr* instr;
    double a, b, value;
    bool bv;
    cxpr_value memo_value;
    cxpr_value scalar_args[CXPR_MAX_CALL_ARGS];

#define CXPR_BOOL_FAST_DISPATCH()                                                   \
    do {                                                                            \
        if (ip >= program->count) {                                                 \
            (void)cxpr_ir_runtime_error(err, "IR program fell off end without return"); \
            return false;                                                           \
        }                                                                           \
        instr = &program->code[ip];                                                 \
        if ((unsigned)instr->op > (unsigned)CXPR_OP_INDEX) {                        \
            goto opb_unsupported;                                                   \
        }                                                                           \
        goto *dispatch[instr->op];                                                  \
    } while (0)
#define CXPR_BOOL_FAST_NEXT()                                                       \
    do {                                                                            \
        ++ip;                                                                       \
        CXPR_BOOL_FAST_DISPATCH();                                                  \
    } while (0)

    CXPR_BOOL_FAST_DISPATCH();

opb_push_const:
    nstack[nsp++] = instr->value;
    CXPR_BOOL_FAST_NEXT();
opb_push_bool:
    bstack[bsp++] = instr->value != 0.0;
    CXPR_BOOL_FAST_NEXT();
opb_load_local:
    if (instr->index >= local_count) {
        (void)cxpr_ir_runtime_error(err, "Unknown local variable");
        return false;
    }
    nstack[nsp++] = locals[instr->index];
    CXPR_BOOL_FAST_NEXT();
opb_load_local_square:
    if (instr->index >= local_count) {
        (void)cxpr_ir_runtime_error(err, "Unknown local variable");
        return false;
    }
    value = locals[instr->index];
    nstack[nsp++] = value * value;
    CXPR_BOOL_FAST_NEXT();
opb_load_var: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_var_map) {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_var_map, fast_var_version, instr,
                                             cache, &value)) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
            return false;
        }
    } else {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, false, &found);
        if (!found) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
            return false;
        }
    }
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
}
opb_load_var_square: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_var_map) {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_var_map, fast_var_version, instr,
                                             cache, &value)) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
            return false;
        }
    } else {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, false, &found);
        if (!found) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
            return false;
        }
    }
    nstack[nsp++] = value * value;
    CXPR_BOOL_FAST_NEXT();
}
opb_load_param: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_param_map) {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_param_map, fast_param_version, instr,
                                             cache, &value)) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            return false;
        }
    } else {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, true, &found);
        if (!found) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            return false;
        }
    }
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
}
opb_load_param_square: {
    bool found = false;
    const double* direct = direct_values ? direct_values[ip] : NULL;
    if (direct) {
        value = *direct;
        found = true;
    } else if (fast_param_map) {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        if (!cxpr_ir_exec_lookup_root_cached(ctx, fast_param_map, fast_param_version, instr,
                                             cache, &value)) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            return false;
        }
    } else {
        cxpr_ir_lookup_cache* cache = cxpr_ir_bindings_cache(program, bindings, ip);
        value = cxpr_ir_exec_lookup_scalar_fast(ctx, instr, cache, true, &found);
        if (!found) {
            (void)cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            return false;
        }
    }
    nstack[nsp++] = value * value;
    CXPR_BOOL_FAST_NEXT();
}
opb_add:
    b = nstack[--nsp];
    a = nstack[--nsp];
    nstack[nsp++] = a + b;
    CXPR_BOOL_FAST_NEXT();
opb_sub:
    b = nstack[--nsp];
    a = nstack[--nsp];
    nstack[nsp++] = a - b;
    CXPR_BOOL_FAST_NEXT();
opb_mul:
    b = nstack[--nsp];
    a = nstack[--nsp];
    nstack[nsp++] = a * b;
    CXPR_BOOL_FAST_NEXT();
opb_div:
    b = nstack[--nsp];
    a = nstack[--nsp];
    if (b == 0.0) {
        if (err) {
            err->code = CXPR_ERR_DIVISION_BY_ZERO;
            err->message = "Division by zero";
        }
        return false;
    }
    nstack[nsp++] = a / b;
    CXPR_BOOL_FAST_NEXT();
opb_mod:
    b = nstack[--nsp];
    a = nstack[--nsp];
    if (b == 0.0) {
        if (err) {
            err->code = CXPR_ERR_DIVISION_BY_ZERO;
            err->message = "Modulo by zero";
        }
        return false;
    }
    nstack[nsp++] = fmod(a, b);
    CXPR_BOOL_FAST_NEXT();
opb_pow:
    b = nstack[--nsp];
    a = nstack[--nsp];
    nstack[nsp++] = pow(a, b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_eq:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a == b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_neq:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a != b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_lt:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a < b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_lte:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a <= b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_gt:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a > b);
    CXPR_BOOL_FAST_NEXT();
opb_cmp_gte:
    b = nstack[--nsp];
    a = nstack[--nsp];
    bstack[bsp++] = (a >= b);
    CXPR_BOOL_FAST_NEXT();
opb_square:
    nstack[nsp - 1] *= nstack[nsp - 1];
    CXPR_BOOL_FAST_NEXT();
opb_not:
    bstack[bsp - 1] = !bstack[bsp - 1];
    CXPR_BOOL_FAST_NEXT();
opb_neg:
    nstack[nsp - 1] = -nstack[nsp - 1];
    CXPR_BOOL_FAST_NEXT();
opb_sign:
    value = nstack[nsp - 1];
    nstack[nsp - 1] = (value > 0.0) - (value < 0.0);
    CXPR_BOOL_FAST_NEXT();
opb_sqrt:
    nstack[nsp - 1] = sqrt(nstack[nsp - 1]);
    CXPR_BOOL_FAST_NEXT();
opb_abs:
    nstack[nsp - 1] = fabs(nstack[nsp - 1]);
    CXPR_BOOL_FAST_NEXT();
opb_floor:
    nstack[nsp - 1] = floor(nstack[nsp - 1]);
    CXPR_BOOL_FAST_NEXT();
opb_ceil:
    nstack[nsp - 1] = ceil(nstack[nsp - 1]);
    CXPR_BOOL_FAST_NEXT();
opb_round:
    nstack[nsp - 1] = round(nstack[nsp - 1]);
    CXPR_BOOL_FAST_NEXT();
opb_clamp:
    b = nstack[--nsp];
    a = nstack[--nsp];
    value = nstack[--nsp];
    if (value < a) value = a;
    if (value > b) value = b;
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
opb_call_unary:
    a = nstack[--nsp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_BOOL) bstack[bsp++] = memo_value.b;
        else if (memo_value.type == CXPR_VALUE_NUMBER) nstack[nsp++] = memo_value.d;
        else {
            (void)cxpr_ir_runtime_error(err, "Memoized function result is not scalar");
            return false;
        }
        CXPR_BOOL_FAST_NEXT();
    }
    value = instr->func->native_scalar.unary(a);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
opb_call_binary:
    b = nstack[--nsp];
    a = nstack[--nsp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_BOOL) bstack[bsp++] = memo_value.b;
        else if (memo_value.type == CXPR_VALUE_NUMBER) nstack[nsp++] = memo_value.d;
        else {
            (void)cxpr_ir_runtime_error(err, "Memoized function result is not scalar");
            return false;
        }
        CXPR_BOOL_FAST_NEXT();
    }
    value = instr->func->native_scalar.binary(a, b);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
opb_call_ternary:
    value = nstack[--nsp];
    b = nstack[--nsp];
    a = nstack[--nsp];
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_BOOL) bstack[bsp++] = memo_value.b;
        else if (memo_value.type == CXPR_VALUE_NUMBER) nstack[nsp++] = memo_value.d;
        else {
            (void)cxpr_ir_runtime_error(err, "Memoized function result is not scalar");
            return false;
        }
        CXPR_BOOL_FAST_NEXT();
    }
    value = instr->func->native_scalar.ternary(a, b, value);
    (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(value));
    nstack[nsp++] = value;
    CXPR_BOOL_FAST_NEXT();
opb_call_func:
    if (instr->func->typed_func) {
        (void)cxpr_ir_runtime_error(err, "Typed function requires typed execution path");
        return false;
    }
    nsp -= instr->index;
    if (cxpr_ir_call_memo_get(ctx, instr, &memo_value)) {
        if (memo_value.type == CXPR_VALUE_BOOL) bstack[bsp++] = memo_value.b;
        else if (memo_value.type == CXPR_VALUE_NUMBER) nstack[nsp++] = memo_value.d;
        else {
            (void)cxpr_ir_runtime_error(err, "Memoized function result is not scalar");
            return false;
        }
        CXPR_BOOL_FAST_NEXT();
    }
    if (instr->func->value_func) {
        for (size_t i = 0; i < instr->index; ++i) {
            scalar_args[i] = cxpr_num(nstack[nsp + i]);
        }
        cxpr_value value_result =
            instr->func->value_func(scalar_args, instr->index, instr->func->userdata);
        if (value_result.type == CXPR_VALUE_BOOL) bstack[bsp++] = value_result.b;
        else if (value_result.type == CXPR_VALUE_NUMBER) nstack[nsp++] = value_result.d;
        else {
            (void)cxpr_ir_runtime_error(err, "Function did not evaluate to scalar");
            return false;
        }
        (void)cxpr_ir_call_memo_set(ctx, instr, value_result);
    } else {
        double call_result = instr->func->sync_func(&nstack[nsp], instr->index, instr->func->userdata);
        (void)cxpr_ir_call_memo_set(ctx, instr, cxpr_num(call_result));
        nstack[nsp++] = call_result;
    }
    CXPR_BOOL_FAST_NEXT();
opb_call_defined: {
    cxpr_value result;
    if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
        nsp -= instr->index;
        if (result.type == CXPR_VALUE_BOOL) bstack[bsp++] = result.b;
        else if (result.type == CXPR_VALUE_NUMBER) nstack[nsp++] = result.d;
        else {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "Memoized defined function returned non-scalar";
            }
            return false;
        }
        CXPR_BOOL_FAST_NEXT();
    }
    for (size_t i = 0; i < instr->index; ++i) {
        scalar_args[i] = cxpr_num(nstack[nsp - instr->index + i]);
    }
    result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, instr->payload, ctx, reg,
                                         scalar_args, instr->index, err);
    if (err && err->code != CXPR_OK) return false;
    nsp -= instr->index;
    if (result.type == CXPR_VALUE_BOOL) bstack[bsp++] = result.b;
    else if (result.type == CXPR_VALUE_NUMBER) nstack[nsp++] = result.d;
    else {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Defined function returned non-scalar";
        }
        return false;
    }
    (void)cxpr_ir_call_memo_set(ctx, instr, result);
    CXPR_BOOL_FAST_NEXT();
}
opb_jump:
    ip = instr->index;
    CXPR_BOOL_FAST_DISPATCH();
opb_jump_if_false:
    bv = bstack[--bsp];
    if (!bv) {
        ip = instr->index;
        CXPR_BOOL_FAST_DISPATCH();
    }
    CXPR_BOOL_FAST_NEXT();
opb_jump_if_true:
    bv = bstack[--bsp];
    if (bv) {
        ip = instr->index;
        CXPR_BOOL_FAST_DISPATCH();
    }
    CXPR_BOOL_FAST_NEXT();
opb_return:
    if (nsp != 0 || bsp != 1) {
        (void)cxpr_ir_runtime_error(err, "IR stack imbalance on bool return");
        return false;
    }
    *out_value = bstack[--bsp];
    return true;
opb_unsupported:
    (void)cxpr_ir_runtime_error(err, "Unsupported bool fast IR opcode");
    return false;

#undef CXPR_BOOL_FAST_NEXT
#undef CXPR_BOOL_FAST_DISPATCH
}

bool cxpr_ir_exec_bool_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                            const cxpr_registry* reg, const double* locals,
                            size_t local_count, bool* out_value, cxpr_error* err) {
    return cxpr_ir_exec_bool_fast_bound(
        program, ctx, reg, locals, local_count, NULL, out_value, err);
}
