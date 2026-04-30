/**
 * @file fast.c
 * @brief Fast scalar IR executor.
 */

#include "internal.h"
#include <math.h>

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

double cxpr_ir_exec_scalar_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
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
        double args[CXPR_MAX_CALL_ARGS];

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
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_VAR_SQUARE: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier").d;
            stack[sp++] = value * value;
            break;
        }
        case CXPR_OP_LOAD_PARAM: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) return cxpr_ir_make_not_found(err, "Unknown parameter variable").d;
            stack[sp++] = value;
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
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
            cxpr_value scalar_args[CXPR_MAX_CALL_ARGS];
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

bool cxpr_ir_exec_bool_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                            const cxpr_registry* reg, const double* locals,
                            size_t local_count, bool* out_value, cxpr_error* err) {
    double nstack[CXPR_IR_STACK_CAPACITY];
    bool bstack[CXPR_IR_STACK_CAPACITY];
    size_t nsp = 0;
    size_t bsp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!out_value) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "Output pointer is NULL";
        }
        return false;
    }
    if (!program || !program->code) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Empty IR program";
        }
        return false;
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        double a, b, value;
        bool bv;
        double args[CXPR_MAX_CALL_ARGS];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            nstack[nsp++] = instr->value;
            break;
        case CXPR_OP_PUSH_BOOL:
            bstack[bsp++] = instr->value != 0.0;
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                (void)cxpr_ir_runtime_error(err, "Unknown local variable");
                return false;
            }
            nstack[nsp++] = locals[instr->index];
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                (void)cxpr_ir_runtime_error(err, "Unknown local variable");
                return false;
            }
            value = locals[instr->index];
            nstack[nsp++] = value * value;
            break;
        case CXPR_OP_LOAD_VAR: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) {
                (void)cxpr_ir_make_not_found(err, "Unknown identifier");
                return false;
            }
            nstack[nsp++] = value;
            break;
        }
        case CXPR_OP_LOAD_VAR_SQUARE: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, false,
                &found);
            if (!found) {
                (void)cxpr_ir_make_not_found(err, "Unknown identifier");
                return false;
            }
            nstack[nsp++] = value * value;
            break;
        }
        case CXPR_OP_LOAD_PARAM: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) {
                (void)cxpr_ir_make_not_found(err, "Unknown parameter variable");
                return false;
            }
            nstack[nsp++] = value;
            break;
        }
        case CXPR_OP_LOAD_PARAM_SQUARE: {
            bool found = false;
            value = cxpr_ir_exec_lookup_scalar_fast(
                ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                &found);
            if (!found) {
                (void)cxpr_ir_make_not_found(err, "Unknown parameter variable");
                return false;
            }
            nstack[nsp++] = value * value;
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
            b = nstack[--nsp];
            a = nstack[--nsp];
            switch (instr->op) {
            case CXPR_OP_ADD: nstack[nsp++] = a + b; break;
            case CXPR_OP_SUB: nstack[nsp++] = a - b; break;
            case CXPR_OP_MUL: nstack[nsp++] = a * b; break;
            case CXPR_OP_DIV:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return false;
                }
                nstack[nsp++] = a / b;
                break;
            case CXPR_OP_MOD:
                if (b == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return false;
                }
                nstack[nsp++] = fmod(a, b);
                break;
            case CXPR_OP_POW: nstack[nsp++] = pow(a, b); break;
            case CXPR_OP_CMP_EQ: bstack[bsp++] = (a == b); break;
            case CXPR_OP_CMP_NEQ: bstack[bsp++] = (a != b); break;
            case CXPR_OP_CMP_LT: bstack[bsp++] = (a < b); break;
            case CXPR_OP_CMP_LTE: bstack[bsp++] = (a <= b); break;
            case CXPR_OP_CMP_GT: bstack[bsp++] = (a > b); break;
            default: bstack[bsp++] = (a >= b); break;
            }
            break;
        case CXPR_OP_SQUARE:
            nstack[nsp - 1] *= nstack[nsp - 1];
            break;
        case CXPR_OP_NOT:
            bstack[bsp - 1] = !bstack[bsp - 1];
            break;
        case CXPR_OP_NEG:
            nstack[nsp - 1] = -nstack[nsp - 1];
            break;
        case CXPR_OP_SIGN:
            value = nstack[nsp - 1];
            nstack[nsp - 1] = (value > 0.0) - (value < 0.0);
            break;
        case CXPR_OP_SQRT:
            nstack[nsp - 1] = sqrt(nstack[nsp - 1]);
            break;
        case CXPR_OP_ABS:
            nstack[nsp - 1] = fabs(nstack[nsp - 1]);
            break;
        case CXPR_OP_FLOOR:
            nstack[nsp - 1] = floor(nstack[nsp - 1]);
            break;
        case CXPR_OP_CEIL:
            nstack[nsp - 1] = ceil(nstack[nsp - 1]);
            break;
        case CXPR_OP_ROUND:
            nstack[nsp - 1] = round(nstack[nsp - 1]);
            break;
        case CXPR_OP_CLAMP:
            b = nstack[--nsp];
            a = nstack[--nsp];
            value = nstack[--nsp];
            if (value < a) value = a;
            if (value > b) value = b;
            nstack[nsp++] = value;
            break;
        case CXPR_OP_CALL_UNARY:
            a = nstack[--nsp];
            nstack[nsp++] = instr->func->native_scalar.unary(a);
            break;
        case CXPR_OP_CALL_BINARY:
            b = nstack[--nsp];
            a = nstack[--nsp];
            nstack[nsp++] = instr->func->native_scalar.binary(a, b);
            break;
        case CXPR_OP_CALL_TERNARY:
            value = nstack[--nsp];
            b = nstack[--nsp];
            a = nstack[--nsp];
            nstack[nsp++] = instr->func->native_scalar.ternary(a, b, value);
            break;
        case CXPR_OP_CALL_FUNC:
            if (instr->func->typed_func) {
                (void)cxpr_ir_runtime_error(err, "Typed function requires typed execution path");
                return false;
            }
            for (size_t i = 0; i < instr->index; ++i) {
                args[i] = nstack[nsp - instr->index + i];
            }
            nsp -= instr->index;
            if (instr->func->value_func) {
                cxpr_value value_result =
                    instr->func->value_func(args, instr->index, instr->func->userdata);
                if (value_result.type == CXPR_VALUE_BOOL) bstack[bsp++] = value_result.b;
                else if (value_result.type == CXPR_VALUE_NUMBER) nstack[nsp++] = value_result.d;
                else {
                    (void)cxpr_ir_runtime_error(err, "Function did not evaluate to scalar");
                    return false;
                }
            } else {
                nstack[nsp++] = instr->func->sync_func(args, instr->index, instr->func->userdata);
            }
            break;
        case CXPR_OP_CALL_DEFINED: {
            cxpr_value result;
            cxpr_value scalar_args[CXPR_MAX_CALL_ARGS];
            for (size_t i = 0; i < instr->index; ++i) {
                scalar_args[i] = cxpr_fv_double(nstack[nsp - instr->index + i]);
            }
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
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
            break;
        }
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
            bv = bstack[--bsp];
            if (!bv) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_JUMP_IF_TRUE:
            bv = bstack[--bsp];
            if (bv) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            if (nsp != 0 || bsp != 1) {
                (void)cxpr_ir_runtime_error(err, "IR stack imbalance on bool return");
                return false;
            }
            *out_value = bstack[--bsp];
            return true;
        default:
            (void)cxpr_ir_runtime_error(err, "Unsupported bool fast IR opcode");
            return false;
        }

        ++ip;
    }

    (void)cxpr_ir_runtime_error(err, "IR program fell off end without return");
    return false;
}
