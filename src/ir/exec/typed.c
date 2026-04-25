/**
 * @file typed.c
 * @brief Typed IR executor.
 */

#include "internal.h"
#include <math.h>

cxpr_value cxpr_ir_exec_typed(const cxpr_ir_program* program, const cxpr_context* ctx,
                              const cxpr_registry* reg, const double* locals,
                              size_t local_count, cxpr_error* err) {
    cxpr_value stack[CXPR_IR_STACK_CAPACITY];
    size_t sp = 0;
    size_t ip = 0;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->code) {
        return cxpr_ir_runtime_error(err, "Empty IR program");
    }

    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        cxpr_value a, b, result;
        cxpr_value typed_args[CXPR_MAX_CALL_ARGS];

        switch (instr->op) {
        case CXPR_OP_PUSH_CONST:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(instr->value),
                                    CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_PUSH_BOOL:
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(instr->value != 0.0),
                                    CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_LOCAL:
            if (instr->index >= local_count) {
                return cxpr_ir_runtime_error(err, "Unknown local variable");
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(locals[instr->index]),
                                    CXPR_IR_STACK_CAPACITY, err)) {
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
                result = cxpr_ir_load_variable_typed(ctx, program, ip, instr, &found);
                if (!found) return cxpr_ir_make_not_found(err, "Unknown identifier");
            }
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_VAR_SQUARE:
            {
                bool found = false;
                result = cxpr_ir_load_variable_typed(ctx, program, ip, instr, &found);
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
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_LOAD_FIELD_SQUARE:
            result = cxpr_ir_load_field_value(ctx, reg, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_push_squared(stack, &sp, result, err)) return cxpr_fv_double(NAN);
            break;
        case CXPR_OP_LOAD_CHAIN:
            result = cxpr_ir_load_chain_value(ctx, instr, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_SQUARE:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Square operation requires double operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_double(a.d * a.d),
                                    CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_NOT:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_BOOL, err,
                                      "Logical not requires bool operand")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp, cxpr_fv_bool(!a.b),
                                    CXPR_IR_STACK_CAPACITY, err)) {
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
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
            if (!cxpr_ir_stack_push(stack, &sp, a, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_UNARY:
            if (!cxpr_ir_pop1(stack, &sp, &a, err)) return cxpr_fv_double(NAN);
            if (!cxpr_ir_require_type(a, CXPR_VALUE_NUMBER, err,
                                      "Function arguments must be doubles")) {
                return cxpr_fv_double(NAN);
            }
            if (!cxpr_ir_stack_push(stack, &sp,
                                    cxpr_fv_double(instr->func->native_scalar.unary(a.d)),
                                    CXPR_IR_STACK_CAPACITY, err)) {
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
                                    CXPR_IR_STACK_CAPACITY, err)) {
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
                    CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_FUNC:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if (instr->index > CXPR_MAX_CALL_ARGS) {
                return cxpr_ir_runtime_error(err, "Too many function arguments");
            }
            for (size_t i = 0; i < instr->index; ++i) {
                typed_args[i] = stack[sp - instr->index + i];
            }
            sp -= instr->index;
            result = cxpr_registry_call_typed(reg, instr->func->name, typed_args, instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_DEFINED:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            result = cxpr_ir_call_defined_scalar((cxpr_func_entry*)instr->func, ctx, reg,
                                                 &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_PRODUCER:
            if (!cxpr_ir_require_stack(sp, instr->index, err)) return cxpr_fv_double(NAN);
            if ((ip + 1) < program->count && program->code[ip + 1].op == CXPR_OP_GET_FIELD) {
                result = cxpr_ir_call_producer_field((cxpr_func_entry*)instr->func, instr->name, ctx,
                                                     &stack[sp - instr->index], instr->index,
                                                     program->code[ip + 1].name, err);
                if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
                sp -= instr->index;
                if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                    return cxpr_fv_double(NAN);
                }
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer((cxpr_func_entry*)instr->func, instr->name, ctx,
                                           &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
                if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                    return cxpr_fv_double(NAN);
                }
                ++ip;
                break;
            }
            result = cxpr_ir_call_producer_cached((cxpr_func_entry*)instr->func,
                                                  instr->func->name, instr->name, ctx,
                                                  &stack[sp - instr->index], instr->index, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            sp -= instr->index;
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
            break;
        case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
            result = cxpr_ir_call_producer_const_field((cxpr_func_entry*)instr->func,
                                                       instr->name, ctx,
                                                       (const double*)instr->payload,
                                                       instr->index, instr->aux_name, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
                if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                    return cxpr_fv_double(NAN);
                }
            }
            break;
        case CXPR_OP_CALL_AST:
            (void)cxpr_eval_ast(instr->ast, ctx, reg, &result, err);
            if (err && err->code != CXPR_OK) return cxpr_fv_double(NAN);
            if (!cxpr_ir_stack_push(stack, &sp, result, CXPR_IR_STACK_CAPACITY, err)) {
                return cxpr_fv_double(NAN);
            }
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
