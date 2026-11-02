/**
 * @file lower.c
 * @brief Immutable executable value-kind metadata for compiled IR.
 */

#include "internal.h"

static cxpr_ir_value_kind cxpr_ir_function_result_kind(const cxpr_ir_instr* instr) {
    if (!instr || !instr->func || !instr->func->has_return_type) {
        return CXPR_IR_VALUE_DYNAMIC;
    }
    switch (instr->func->return_type) {
    case CXPR_VALUE_NUMBER: return CXPR_IR_VALUE_F64;
    case CXPR_VALUE_INT64: return CXPR_IR_VALUE_I64;
    case CXPR_VALUE_BOOL: return CXPR_IR_VALUE_BOOL;
    default: return CXPR_IR_VALUE_DYNAMIC;
    }
}

static cxpr_ir_value_kind cxpr_ir_instruction_value_kind(
    const cxpr_ir_program* program, const cxpr_ir_instr* instr) {
    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
        return CXPR_IR_VALUE_F64;
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        return program->fast_result_kind == CXPR_IR_RESULT_UNKNOWN
                   ? CXPR_IR_VALUE_DYNAMIC : CXPR_IR_VALUE_F64;
    case CXPR_OP_LOOKBACK_RESOLVE:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_POW:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
    case CXPR_OP_CLAMP:
    case CXPR_OP_CALL_UNARY:
    case CXPR_OP_CALL_BINARY:
    case CXPR_OP_CALL_TERNARY:
        return CXPR_IR_VALUE_F64;
    case CXPR_OP_PUSH_INT64:
        return CXPR_IR_VALUE_I64;
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
    case CXPR_OP_NOT:
        return CXPR_IR_VALUE_BOOL;
    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_SQUARE:
    case CXPR_OP_NEG:
        return program->int64_fast_compatible ? CXPR_IR_VALUE_I64 :
               program->fast_result_kind == CXPR_IR_RESULT_DOUBLE ? CXPR_IR_VALUE_F64 :
                                                                    CXPR_IR_VALUE_DYNAMIC;
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        return cxpr_ir_function_result_kind(instr);
    case CXPR_OP_LOAD_TEMP:
        return CXPR_IR_VALUE_F64;
    case CXPR_OP_RETURN:
        return program->fast_result_kind == CXPR_IR_RESULT_DOUBLE ? CXPR_IR_VALUE_F64 :
               program->fast_result_kind == CXPR_IR_RESULT_INT64 ? CXPR_IR_VALUE_I64 :
               program->fast_result_kind == CXPR_IR_RESULT_BOOL ? CXPR_IR_VALUE_BOOL :
                                                                  CXPR_IR_VALUE_DYNAMIC;
    case CXPR_OP_JUMP:
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
    case CXPR_OP_LOOKBACK_PUSH:
    case CXPR_OP_LOOKBACK_POP:
    case CXPR_OP_STORE_LOCAL:
    case CXPR_OP_STORE_TEMP:
        return CXPR_IR_VALUE_NONE;
    default:
        return CXPR_IR_VALUE_DYNAMIC;
    }
}

bool cxpr_ir_program_lower_value_kinds(cxpr_ir_program* program, cxpr_error* err) {
    unsigned char* kinds;

    if (!program) return false;
    free(program->value_kinds);
    program->value_kinds = NULL;
    if (program->count == 0u) return true;
    kinds = (unsigned char*)malloc(program->count * sizeof(*kinds));
    if (!kinds) {
        if (err) {
            err->code = CXPR_ERR_OUT_OF_MEMORY;
            err->message = "Out of memory";
        }
        return false;
    }
    for (size_t i = 0u; i < program->count; ++i) {
        kinds[i] = (unsigned char)cxpr_ir_instruction_value_kind(
            program, &program->code[i]);
    }
    program->value_kinds = kinds;
    return true;
}

