/**
 * @file int64.c
 * @brief Closed int64/bool IR execution without cxpr_value construction.
 */

#include "internal.h"

static bool cxpr_ir_int64_error(cxpr_error* err, cxpr_error_code code,
                                const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return false;
}

bool cxpr_ir_validate_int64_fast_program(const cxpr_ir_program* program) {
    bool has_int64 = false;

    if (!program || !program->code ||
        (program->fast_result_kind != CXPR_IR_RESULT_INT64 &&
         program->fast_result_kind != CXPR_IR_RESULT_BOOL)) return false;
    for (size_t i = 0u; i < program->count; ++i) {
        switch (program->code[i].op) {
        case CXPR_OP_PUSH_INT64:
            has_int64 = true;
            break;
        case CXPR_OP_PUSH_BOOL:
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
        case CXPR_OP_SQUARE:
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
        case CXPR_OP_NOT:
        case CXPR_OP_NEG:
        case CXPR_OP_JUMP:
        case CXPR_OP_JUMP_IF_FALSE:
        case CXPR_OP_JUMP_IF_TRUE:
        case CXPR_OP_RETURN:
            break;
        default:
            return false;
        }
    }
    return has_int64;
}

static bool cxpr_ir_exec_int64_lowered(const cxpr_ir_program* program,
                                       int64_t* out_int64, bool* out_bool,
                                       cxpr_error* err) {
    int64_t istack[CXPR_IR_STACK_CAPACITY];
    bool bstack[CXPR_IR_STACK_CAPACITY];
    size_t isp = 0u;
    size_t bsp = 0u;
    size_t ip = 0u;

    if (err) *err = (cxpr_error){0};
    if (!program || !program->int64_fast_compatible) {
        return cxpr_ir_int64_error(err, CXPR_ERR_TYPE_MISMATCH,
                                   "Program is not closed int64 IR");
    }
    while (ip < program->count) {
        const cxpr_ir_instr* instr = &program->code[ip];
        int64_t left;
        int64_t right;
        int64_t result;
        bool condition;

        switch (instr->op) {
        case CXPR_OP_PUSH_INT64:
            istack[isp++] = instr->int64_value;
            break;
        case CXPR_OP_PUSH_BOOL:
            bstack[bsp++] = instr->value != 0.0;
            break;
        case CXPR_OP_ADD:
        case CXPR_OP_SUB:
        case CXPR_OP_MUL:
            if (isp < 2u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR int64 stack underflow");
            right = istack[--isp];
            left = istack[--isp];
            if ((instr->op == CXPR_OP_ADD && __builtin_add_overflow(left, right, &result)) ||
                (instr->op == CXPR_OP_SUB && __builtin_sub_overflow(left, right, &result)) ||
                (instr->op == CXPR_OP_MUL && __builtin_mul_overflow(left, right, &result))) {
                return cxpr_ir_int64_error(
                    err, CXPR_ERR_OVERFLOW,
                    instr->op == CXPR_OP_ADD ? "int64 addition overflow" :
                    instr->op == CXPR_OP_SUB ? "int64 subtraction overflow" :
                                               "int64 multiplication overflow");
            }
            istack[isp++] = result;
            break;
        case CXPR_OP_SQUARE:
            if (isp == 0u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR int64 stack underflow");
            left = istack[isp - 1u];
            if (__builtin_mul_overflow(left, left, &result)) {
                return cxpr_ir_int64_error(err, CXPR_ERR_OVERFLOW,
                                           "int64 multiplication overflow");
            }
            istack[isp - 1u] = result;
            break;
        case CXPR_OP_NEG:
            if (isp == 0u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR int64 stack underflow");
            if (istack[isp - 1u] == INT64_MIN) return cxpr_ir_int64_error(
                err, CXPR_ERR_OVERFLOW, "int64 negation overflow");
            istack[isp - 1u] = -istack[isp - 1u];
            break;
        case CXPR_OP_CMP_EQ:
        case CXPR_OP_CMP_NEQ:
        case CXPR_OP_CMP_LT:
        case CXPR_OP_CMP_LTE:
        case CXPR_OP_CMP_GT:
        case CXPR_OP_CMP_GTE:
            if (isp < 2u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR int64 stack underflow");
            right = istack[--isp];
            left = istack[--isp];
            condition = instr->op == CXPR_OP_CMP_EQ ? left == right :
                        instr->op == CXPR_OP_CMP_NEQ ? left != right :
                        instr->op == CXPR_OP_CMP_LT ? left < right :
                        instr->op == CXPR_OP_CMP_LTE ? left <= right :
                        instr->op == CXPR_OP_CMP_GT ? left > right : left >= right;
            bstack[bsp++] = condition;
            break;
        case CXPR_OP_NOT:
            if (bsp == 0u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR bool stack underflow");
            bstack[bsp - 1u] = !bstack[bsp - 1u];
            break;
        case CXPR_OP_JUMP:
            ip = instr->index;
            continue;
        case CXPR_OP_JUMP_IF_FALSE:
        case CXPR_OP_JUMP_IF_TRUE:
            if (bsp == 0u) return cxpr_ir_int64_error(
                err, CXPR_ERR_SYNTAX, "IR bool stack underflow");
            condition = bstack[--bsp];
            if ((instr->op == CXPR_OP_JUMP_IF_FALSE && !condition) ||
                (instr->op == CXPR_OP_JUMP_IF_TRUE && condition)) {
                ip = instr->index;
                continue;
            }
            break;
        case CXPR_OP_RETURN:
            if (program->fast_result_kind == CXPR_IR_RESULT_INT64 && out_int64 && isp == 1u) {
                *out_int64 = istack[0];
                return true;
            }
            if (program->fast_result_kind == CXPR_IR_RESULT_BOOL && out_bool && bsp == 1u) {
                *out_bool = bstack[0];
                return true;
            }
            return cxpr_ir_int64_error(err, CXPR_ERR_TYPE_MISMATCH,
                                       "Lowered IR result kind mismatch");
        default:
            return cxpr_ir_int64_error(err, CXPR_ERR_TYPE_MISMATCH,
                                       "Unsupported lowered int64 opcode");
        }
        ++ip;
    }
    return cxpr_ir_int64_error(err, CXPR_ERR_SYNTAX,
                               "IR program fell off end without return");
}

bool cxpr_ir_exec_int64_fast(const cxpr_ir_program* program, const cxpr_context* ctx,
                             const cxpr_registry* reg, const double* locals,
                             size_t local_count, int64_t* out_value, cxpr_error* err) {
    if (!out_value) return cxpr_ir_int64_error(
        err, CXPR_ERR_TYPE_MISMATCH, "Output pointer is NULL");
    if (program && program->int64_fast_compatible) {
        return cxpr_ir_exec_int64_lowered(program, out_value, NULL, err);
    }
    {
        cxpr_value value = cxpr_ir_exec_typed(program, ctx, reg, locals, local_count, err);
        if (err && err->code != CXPR_OK) return false;
        if (value.type != CXPR_VALUE_INT64) return cxpr_ir_int64_error(
            err, CXPR_ERR_TYPE_MISMATCH, "Expression did not evaluate to int64");
        *out_value = value.i64;
        return true;
    }
}

bool cxpr_ir_exec_int64_bool_fast(const cxpr_ir_program* program,
                                  bool* out_value, cxpr_error* err) {
    if (!out_value) return cxpr_ir_int64_error(
        err, CXPR_ERR_TYPE_MISMATCH, "Output pointer is NULL");
    return cxpr_ir_exec_int64_lowered(program, NULL, out_value, err);
}

