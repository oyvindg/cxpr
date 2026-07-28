/**
 * @file typed.c
 * @brief Typed IR executor.
 */

#include "internal.h"
#include "core.h"
#include "ast/internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

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

static bool cxpr_ir_resolve_lookback_target(const cxpr_expr_ast* target,
                                            size_t offset,
                                            const cxpr_context* ctx,
                                            const cxpr_registry* reg,
                                            cxpr_value* out,
                                            cxpr_error* err) {
    cxpr_expr_ast index_ast = {0};

    if (!reg || !reg->lookback_resolver || !target || !out) {
        return false;
    }
    index_ast.type = CXPR_NODE_NUMBER;
    index_ast.data.number.value = (double)offset;
    return reg->lookback_resolver(
        target, &index_ast, ctx, reg, reg->lookback_userdata, out, err);
}

static bool cxpr_ir_resolve_lookback_instr(const cxpr_ir_instr* instr,
                                           size_t offset,
                                           const cxpr_context* ctx,
                                           const cxpr_registry* reg,
                                           cxpr_value* out,
                                           cxpr_error* err) {
    const cxpr_expr_ast* borrowed = instr ? (const cxpr_expr_ast*)instr->payload : NULL;
    cxpr_expr_ast target = {0};
    cxpr_expr_ast* target_ptr = (cxpr_expr_ast*)borrowed;
    char field_key[256];
    char chain_key[512];
    char* segments[32];
    size_t depth = 0u;

    if (!instr) return false;
    if (borrowed) {
        return cxpr_ir_resolve_lookback_target(borrowed, offset, ctx, reg, out, err);
    }

    switch (instr->op) {
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
        target.type = CXPR_NODE_IDENTIFIER;
        target.data.identifier.name = (char*)instr->name;
        break;
    case CXPR_OP_LOAD_FIELD:
    case CXPR_OP_LOAD_FIELD_SQUARE: {
        char* dot;
        if (!instr->name) return false;
        if (strlen(instr->name) >= sizeof(field_key)) return false;
        strcpy(field_key, instr->name);
        dot = strchr(field_key, '.');
        if (!dot || dot == field_key || dot[1] == '\0') return false;
        *dot = '\0';
        target.type = CXPR_NODE_FIELD_ACCESS;
        target.data.field_access.object = field_key;
        target.data.field_access.field = dot + 1;
        target.data.field_access.full_key = (char*)instr->name;
        break;
    }
    case CXPR_OP_LOAD_NAMED_FIELD:
        target.type = CXPR_NODE_FIELD_ACCESS;
        target.data.field_access.object = (char*)instr->name;
        target.data.field_access.field = (char*)instr->aux_name;
        target.data.field_access.full_key = NULL;
        break;
    case CXPR_OP_LOAD_CHAIN: {
        char* segment;
        char* saveptr = NULL;
        if (!instr->name) return false;
        if (strlen(instr->name) >= sizeof(chain_key)) return false;
        strcpy(chain_key, instr->name);
        segment = cxpr_strtok_r(chain_key, ".", &saveptr);
        while (segment && depth < sizeof(segments) / sizeof(segments[0])) {
            segments[depth++] = segment;
            segment = cxpr_strtok_r(NULL, ".", &saveptr);
        }
        if (segment || depth < 2u) return false;
        target.type = CXPR_NODE_CHAIN_ACCESS;
        target.data.chain_access.path = segments;
        target.data.chain_access.depth = depth;
        target.data.chain_access.full_key = (char*)instr->name;
        break;
    }
    default:
        return false;
    }

    target_ptr = &target;
    return cxpr_ir_resolve_lookback_target(target_ptr, offset, ctx, reg, out, err);
}

static void cxpr_ir_release_owned_stack_values(cxpr_value* stack,
                                               bool* owned,
                                               size_t start,
                                               size_t count) {
    for (size_t i = 0u; i < count; ++i) {
        size_t index = start + i;
        if (owned[index]) {
            cxpr_value_free(&stack[index]);
            owned[index] = false;
        }
    }
}

cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                              const cxpr_registry* reg, const double* locals,
                              size_t local_count, cxpr_error* err) {
    cxpr_value stack[CXPR_IR_STACK_CAPACITY];
    bool stack_owned[CXPR_IR_STACK_CAPACITY] = {false};
    size_t lookback_stack[CXPR_IR_STACK_CAPACITY];
    size_t sp = 0;
    size_t lookback_sp = 0;
    size_t lookback_offset = 0;
    size_t lookback_depth = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        return cxpr_ir_runtime_error(err, "Empty IR program");
    }

#define CXPR_TYPED_PUSH(VALUE_EXPR)                                                     \
    do {                                                                                \
        if (!cxpr_ir_stack_push(stack, &sp, (VALUE_EXPR), CXPR_IR_STACK_CAPACITY, err)) { \
            return cxpr_num(NAN);                                                       \
        }                                                                               \
        stack_owned[sp - 1u] = false;                                                   \
    } while (0)

#define CXPR_TYPED_PUSH_OWNED(VALUE_EXPR)                                               \
    do {                                                                                \
        if (!cxpr_ir_stack_push(stack, &sp, (VALUE_EXPR), CXPR_IR_STACK_CAPACITY, err)) { \
            return cxpr_num(NAN);                                                       \
        }                                                                               \
        stack_owned[sp - 1u] = true;                                                    \
    } while (0)

#define CXPR_TYPED_PUSH_SQUARED(VALUE_EXPR)                                             \
    do {                                                                                \
        if (!cxpr_ir_push_squared(stack, &sp, (VALUE_EXPR), err)) {                     \
            return cxpr_num(NAN);                                                       \
        }                                                                               \
        stack_owned[sp - 1u] = false;                                                   \
    } while (0)

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        cxpr_value a, b, result;
        cxpr_value typed_args[CXPR_MAX_CALL_ARGS];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            CXPR_TYPED_PUSH(cxpr_num(instr->value));
            break;
        case CXPR_OP_PUSH_BOOL:
            CXPR_TYPED_PUSH(cxpr_bool(instr->value != 0.0));
            break;
        case CXPR_OP_PUSH_STRING:
            CXPR_TYPED_PUSH(cxpr_string(instr->name));
            break;
        case CXPR_OP_BUILD_ARRAY:
            {
                size_t start;
                cxpr_array_value* array;
                if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_num(NAN);
                start = sp - instr->index;
                array = cxpr_array_value_new(instr->index ? &stack[start] : NULL, instr->index);
                cxpr_ir_release_owned_stack_values(stack, stack_owned, start, instr->index);
                sp = start;
                if (!array) {
                    if (err) {
                        err->code = CXPR_ERR_OUT_OF_MEMORY;
                        err->message = "Out of memory";
                    }
                    return cxpr_num(NAN);
                }
                CXPR_TYPED_PUSH_OWNED(cxpr_array(array));
            }
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            CXPR_TYPED_PUSH(cxpr_num(locals[instr->index]));
            break;
        case CXPR_OP_LOAD_LOCAL_SQUARE:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            CXPR_TYPED_PUSH_SQUARED(cxpr_num(locals[instr->index]));
            break;
        case CXPR_OP_LOAD_VAR:
            {
                bool found = false;
                if (lookback_depth > 0u) {
                    if (!cxpr_ir_resolve_lookback_instr(
                            instr, lookback_offset, ctx, reg, &result, err)) {
                        if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                        return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                    }
                } else {
                    result = cxpr_ir_load_variable_typed(ctx, program, ip, instr, &found);
                    if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
                }
            }
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_LOAD_VAR_SQUARE:
            {
                bool found = false;
                if (lookback_depth > 0u) {
                    if (!cxpr_ir_resolve_lookback_instr(
                            instr, lookback_offset, ctx, reg, &result, err)) {
                        if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                        return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                    }
                } else {
                    result = cxpr_ir_load_variable_typed(ctx, program, ip, instr, &found);
                    if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_identifier_message(instr));
                }
            }
            CXPR_TYPED_PUSH_SQUARED(result);
            break;
        case CXPR_OP_LOAD_PARAM:
            {
                bool found = false;
                bool bool_value = cxpr_context_get_param_bool(ctx, instr->name, &found);
                if (found) {
                    result = cxpr_bool(bool_value);
                } else {
                    const char* string_value = cxpr_context_get_param_string(ctx, instr->name, &found);
                    if (found) {
                        result = cxpr_string(string_value);
                    } else {
                        result = cxpr_num(cxpr_ir_lookup_cached_scalar(
                            ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                            &found));
                    }
                }
                if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            }
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_LOAD_PARAM_SQUARE:
            {
                bool found = false;
                result = cxpr_num(cxpr_ir_lookup_cached_scalar(
                    ctx, instr, program->lookup_cache ? &program->lookup_cache[ip] : NULL, true,
                    &found));
                if (!found) return cxpr_ir_make_not_found(err, cxpr_ir_unknown_param_message(instr));
            }
            CXPR_TYPED_PUSH_SQUARED(result);
            break;
        case CXPR_OP_LOAD_FIELD:
            if (lookback_depth > 0u) {
                if (!cxpr_ir_resolve_lookback_instr(
                        instr, lookback_offset, ctx, reg, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
            } else {
                result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_LOAD_FIELD_SQUARE:
            if (lookback_depth > 0u) {
                if (!cxpr_ir_resolve_lookback_instr(
                        instr, lookback_offset, ctx, reg, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
            } else {
                result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH_SQUARED(result);
            break;
        case CXPR_OP_LOAD_NAMED_FIELD:
            if (lookback_depth > 0u) {
                if (!cxpr_ir_resolve_lookback_instr(
                        instr, lookback_offset, ctx, reg, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
            } else {
                result = cxpr_ir_load_named_field_value(ctx, instr, err);
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_LOAD_CHAIN:
            if (lookback_depth > 0u) {
                if (!cxpr_ir_resolve_lookback_instr(
                        instr, lookback_offset, ctx, reg, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
            } else {
                result = cxpr_ir_load_chain_value(ctx, instr, err);
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH(result);
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
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_num(NAN);
            switch (instr->op) {
            case CXPR_OP_ADD:
            case CXPR_OP_SUB:
            case CXPR_OP_MUL:
            case CXPR_OP_DIV:
            case CXPR_OP_MOD:
            case CXPR_OP_POW:
                if (a.type != CXPR_VALUE_NUMBER || b.type != CXPR_VALUE_NUMBER) {
                    cxpr_valop vop;
                    bool mappable = true;
                    switch (instr->op) {
                    case CXPR_OP_ADD: vop = CXPR_VALOP_ADD; break;
                    case CXPR_OP_SUB: vop = CXPR_VALOP_SUB; break;
                    case CXPR_OP_MUL: vop = CXPR_VALOP_MUL; break;
                    case CXPR_OP_DIV: vop = CXPR_VALOP_DIV; break;
                    default: mappable = false; vop = CXPR_VALOP_ADD; break;
                    }
                    if (mappable) {
                        int handled = cxpr_value_binary_op(vop, a, b, &result, err);
                        if (handled == 1) break;
                        if (handled == -1) return cxpr_num(NAN);
                    }
                    (void)cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                               "Arithmetic requires double operands");
                    (void)cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                               "Arithmetic requires double operands");
                    return cxpr_num(NAN);
                }
                if (instr->op == CXPR_OP_DIV && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Division by zero";
                    }
                    return cxpr_num(NAN);
                }
                if (instr->op == CXPR_OP_MOD && b.d == 0.0) {
                    if (err) {
                        err->code = CXPR_ERR_DIVISION_BY_ZERO;
                        err->message = "Modulo by zero";
                    }
                    return cxpr_num(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_ADD: result = cxpr_num(a.d + b.d); break;
                case CXPR_OP_SUB: result = cxpr_num(a.d - b.d); break;
                case CXPR_OP_MUL: result = cxpr_num(a.d * b.d); break;
                case CXPR_OP_DIV: result = cxpr_num(a.d / b.d); break;
                case CXPR_OP_MOD: result = cxpr_num(fmod(a.d, b.d)); break;
                default: result = cxpr_num(pow(a.d, b.d)); break;
                }
                break;
            case CXPR_OP_CMP_EQ:
            case CXPR_OP_CMP_NEQ:
                if (a.type != b.type ||
                    (a.type != CXPR_VALUE_NUMBER && a.type != CXPR_VALUE_BOOL &&
                     a.type != CXPR_VALUE_STRING && a.type != CXPR_VALUE_NULL &&
                     a.type != CXPR_VALUE_TIMESTAMP && a.type != CXPR_VALUE_DURATION)) {
                    if (err) {
                        err->code = CXPR_ERR_TYPE_MISMATCH;
                        err->message = "Equality requires matching scalar operands";
                    }
                    return cxpr_num(NAN);
                }
                if (a.type == CXPR_VALUE_NUMBER) {
                    result = cxpr_bool(instr->op == CXPR_OP_CMP_EQ ? (a.d == b.d) : (a.d != b.d));
                } else if (a.type == CXPR_VALUE_BOOL) {
                    result = cxpr_bool(instr->op == CXPR_OP_CMP_EQ ? (a.b == b.b) : (a.b != b.b));
                } else if (a.type == CXPR_VALUE_STRING) {
                    result = cxpr_bool(instr->op == CXPR_OP_CMP_EQ
                                           ? (strcmp(a.str, b.str) == 0)
                                           : (strcmp(a.str, b.str) != 0));
                } else if (a.type == CXPR_VALUE_NULL) {
                    result = cxpr_bool(instr->op == CXPR_OP_CMP_EQ);
                } else {
                    result = cxpr_bool(instr->op == CXPR_OP_CMP_EQ ? (a.i64 == b.i64) : (a.i64 != b.i64));
                }
                break;
            default:
                if (a.type != CXPR_VALUE_NUMBER || b.type != CXPR_VALUE_NUMBER) {
                    cxpr_valop vop;
                    int handled;
                    switch (instr->op) {
                    case CXPR_OP_CMP_LT: vop = CXPR_VALOP_LT; break;
                    case CXPR_OP_CMP_LTE: vop = CXPR_VALOP_LTE; break;
                    case CXPR_OP_CMP_GT: vop = CXPR_VALOP_GT; break;
                    default: vop = CXPR_VALOP_GTE; break;
                    }
                    handled = cxpr_value_binary_op(vop, a, b, &result, err);
                    if (handled == 1) break;
                    if (handled == -1) return cxpr_num(NAN);
                    (void)cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                               "Comparison requires double operands");
                    (void)cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                               "Comparison requires double operands");
                    return cxpr_num(NAN);
                }
                switch (instr->op) {
                case CXPR_OP_CMP_LT: result = cxpr_bool(a.d < b.d); break;
                case CXPR_OP_CMP_LTE: result = cxpr_bool(a.d <= b.d); break;
                case CXPR_OP_CMP_GT: result = cxpr_bool(a.d > b.d); break;
                default: result = cxpr_bool(a.d >= b.d); break;
                }
                break;
            }
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_SQUARE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Square operation requires double operand")) {
                return cxpr_num(NAN);
            }
            CXPR_TYPED_PUSH(cxpr_num(a.d * a.d));
            break;
        case CXPR_OP_NOT:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_BOOL, err,
                                      "Logical not requires bool operand")) {
                return cxpr_num(NAN);
            }
            CXPR_TYPED_PUSH(cxpr_bool(!a.b));
            break;
        case CXPR_OP_NEG:
        case CXPR_OP_SIGN:
        case CXPR_OP_SQRT:
        case CXPR_OP_ABS:
        case CXPR_OP_FLOOR:
        case CXPR_OP_CEIL:
        case CXPR_OP_ROUND:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Numeric intrinsic requires double operand")) {
                return cxpr_num(NAN);
            }
            switch (instr->op) {
            case CXPR_OP_NEG: result = cxpr_num(-a.d); break;
            case CXPR_OP_SIGN: result = cxpr_num((a.d > 0.0) - (a.d < 0.0)); break;
            case CXPR_OP_SQRT: result = cxpr_num(sqrt(a.d)); break;
            case CXPR_OP_ABS: result = cxpr_num(fabs(a.d)); break;
            case CXPR_OP_FLOOR: result = cxpr_num(floor(a.d)); break;
            case CXPR_OP_CEIL: result = cxpr_num(ceil(a.d)); break;
            default: result = cxpr_num(round(a.d)); break;
            }
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CLAMP:
            if (!cxpr_ir_pop2(stack, &sp, &result, &b, err)) return cxpr_num(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(result, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "clamp() requires double operands")) {
                return cxpr_num(NAN);
            }
            if (a.d < result.d) a.d = result.d;
            if (a.d > b.d) a.d = b.d;
            CXPR_TYPED_PUSH(a);
            break;
        case CXPR_OP_CALL_UNARY:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_num(NAN);
            }
            if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
                CXPR_TYPED_PUSH(result);
                break;
            }
            result = cxpr_num(instr->func->native_scalar.unary(a.d));
            (void)cxpr_ir_call_memo_set(ctx, instr, result);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_BINARY:
            if (!cxpr_ir_pop2(stack, &sp, &a, &b, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_num(NAN);
            }
            if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
                CXPR_TYPED_PUSH(result);
                break;
            }
            result = cxpr_num(instr->func->native_scalar.binary(a.d, b.d));
            (void)cxpr_ir_call_memo_set(ctx, instr, result);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_TERNARY:
            if (!cxpr_ir_pop2(stack, &sp, &b, &result, err)) return cxpr_num(NAN);
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(b, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles") ||
                !cxpr_ir_require_type(result, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_num(NAN);
            }
            if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
                CXPR_TYPED_PUSH(result);
                break;
            }
            result = cxpr_num(instr->func->native_scalar.ternary(a.d, b.d, result.d));
            (void)cxpr_ir_call_memo_set(ctx, instr, result);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_FUNC:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_num(NAN);
            if (instr->index > CXPR_MAX_CALL_ARGS) {
                return cxpr_ir_runtime_error(err, "Too many function arguments");
            }
            for (size_t i = 0; i < instr->index; ++i) {
                typed_args[i] = stack[sp - instr->index + i];
            }
            sp -= instr->index;
            if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
                CXPR_TYPED_PUSH(result);
                break;
            }
            result = cxpr_registry_call_typed(reg, instr->func->name, typed_args, instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            (void)cxpr_ir_call_memo_set(ctx, instr, result);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_DEFINED:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_num(NAN);
            if (cxpr_ir_call_memo_get(ctx, instr, &result)) {
                sp -= instr->index;
                CXPR_TYPED_PUSH(result);
                break;
            }
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, instr->payload, ctx, reg,
                                                 &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            sp -= instr->index;
            (void)cxpr_ir_call_memo_set(ctx, instr, result);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_PRODUCER:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_num(NAN);
            if ((ip + 1) < program->count && program->code[ip + 1].op == CXPR_OP_GET_FIELD) {
                result = cxpr_ir_call_producer_field((cxpr_func_entry*)instr->func, instr->name, ctx,
                                                     &stack[sp - instr->index], instr->index,
                                                     program->code[ip + 1].name, err);
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                sp -= instr->index;
                CXPR_TYPED_PUSH(result);
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer((cxpr_func_entry*)instr->func, instr->name, ctx,
                                           &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            sp -= instr->index;
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_PRODUCER_CONST:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_num(NAN);
            if ((ip + 1) < program->count && program->code[ip + 1].op == CXPR_OP_GET_FIELD) {
                result = cxpr_ir_call_producer_field_cached((cxpr_func_entry*)instr->func,
                                                            instr->func->name, instr->name, ctx,
                                                            &stack[sp - instr->index], instr->index,
                                                            program->code[ip + 1].name, err);
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                sp -= instr->index;
                CXPR_TYPED_PUSH(result);
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer_cached((cxpr_func_entry*)instr->func,
                                                  instr->func->name, instr->name, ctx,
                                                  &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            sp -= instr->index;
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
            result = cxpr_ir_call_producer_const_field((cxpr_func_entry*)instr->func,
                                                       instr->name, ctx,
                                                       (const double*)instr->payload,
                                                       instr->index, instr->aux_name, err);
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_GET_FIELD:
            {
                bool found = false;

                if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
                if (!cxpr_ir_require_type(a, CXPR_VALUE_STRUCT, err,
                                          "Field access requires struct operand")) {
                    return cxpr_num(NAN);
                }
                result = cxpr_ir_struct_get_field(a.s, instr->name, &found);
                if (!found) return cxpr_ir_make_not_found(err, "Unknown field access");
                CXPR_TYPED_PUSH(result);
            }
            break;
        case CXPR_OP_CALL_AST:
            if (lookback_depth > 0u) {
                if (!cxpr_ir_resolve_lookback_target(
                        instr->ast, lookback_offset, ctx, reg, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
            } else {
                (void)cxpr_eval_ast(instr->ast, ctx, reg, &result, err);
            }
            if (err && err->code != CXPR_OK) return cxpr_num(NAN);
            CXPR_TYPED_PUSH(result);
            break;
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
        case CXPR_OP_JUMP_IF_TRUE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_num(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_BOOL, err,
                                      "Conditional jump requires bool operand")) {
                return cxpr_num(NAN);
            }
            {
                if ((instr->op == CXPR_OP_JUMP_IF_FALSE && !a.b) ||
                    (instr->op == CXPR_OP_JUMP_IF_TRUE && a.b)) {
                ip = instr->index;
                continue;
                }
            }
            break;
        case CXPR_OP_LOOKBACK_PUSH:
            if (lookback_sp >= CXPR_IR_STACK_CAPACITY) {
                return cxpr_ir_runtime_error(err, "Lookback offset stack overflow");
            }
            lookback_stack[lookback_sp++] = lookback_offset;
            if (((size_t)-1) - lookback_offset < instr->index) {
                return cxpr_ir_runtime_error(err, "Lookback offset overflow");
            }
            lookback_offset += instr->index;
            ++lookback_depth;
            break;
        case CXPR_OP_LOOKBACK_POP:
            if (lookback_sp == 0) {
                return cxpr_ir_runtime_error(err, "Lookback offset stack underflow");
            }
            lookback_offset = lookback_stack[--lookback_sp];
            if (lookback_depth == 0u) {
                return cxpr_ir_runtime_error(err, "Lookback depth underflow");
            }
            --lookback_depth;
            break;
        case CXPR_OP_LOOKBACK_RESOLVE:
            {
                const cxpr_expr_ast* target = (const cxpr_expr_ast*)instr->payload;
                cxpr_expr_ast index_ast = {0};
                result = cxpr_num(NAN);
                if (!reg || !reg->lookback_resolver || !target) {
                    return cxpr_ir_runtime_error(err, "Lookback requires registry resolver");
                }
                if (((size_t)-1) - lookback_offset < instr->index) {
                    return cxpr_ir_runtime_error(err, "Lookback offset overflow");
                }
                index_ast.type = CXPR_NODE_NUMBER;
                index_ast.data.number.value = (double)(lookback_offset + instr->index);
                if (!reg->lookback_resolver(target, &index_ast, ctx, reg,
                                            reg->lookback_userdata, &result, err)) {
                    if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                    return cxpr_ir_runtime_error(err, "Lookback resolver failed");
                }
                if (err && err->code != CXPR_OK) return cxpr_num(NAN);
                CXPR_TYPED_PUSH(result);
            }
            break;
        case CXPR_OP_STORE_LOCAL:
            return cxpr_ir_runtime_error(err, "STORE_LOCAL requires scalar fast execution");
        case CXPR_OP_RETURN:
            if (!cxpr_ir_pop1(stack, &sp, &result, err)) return cxpr_num(NAN);
            if (sp != 0) {
                return cxpr_ir_runtime_error(err, "IR stack not empty at return");
            }
            return result;
        default:
            return cxpr_ir_runtime_error(err, "Unsupported IR opcode");
        }

        ++ip;
    }

#undef CXPR_TYPED_PUSH
#undef CXPR_TYPED_PUSH_OWNED
#undef CXPR_TYPED_PUSH_SQUARED

    return cxpr_ir_runtime_error(err, "IR program fell off end without return");
}
