/**
 * @file view.c
 * @brief Public read-only view helpers for compiled cxpr IR programs.
 */

#include <cxpr/ir.h>

#include "internal.h"

static cxpr_ir_opcode cxpr_ir_view_map_opcode(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_PUSH_CONST: return CXPR_IR_VIEW_OP_PUSH_CONST;
    case CXPR_OP_PUSH_BOOL: return CXPR_IR_VIEW_OP_PUSH_BOOL;
    case CXPR_OP_PUSH_STRING: return CXPR_IR_VIEW_OP_PUSH_STRING;
    case CXPR_OP_BUILD_ARRAY: return CXPR_IR_VIEW_OP_BUILD_ARRAY;
    case CXPR_OP_LOAD_LOCAL: return CXPR_IR_VIEW_OP_LOAD_LOCAL;
    case CXPR_OP_LOAD_LOCAL_SQUARE: return CXPR_IR_VIEW_OP_LOAD_LOCAL_SQUARE;
    case CXPR_OP_LOAD_VAR: return CXPR_IR_VIEW_OP_LOAD_VAR;
    case CXPR_OP_LOAD_VAR_SQUARE: return CXPR_IR_VIEW_OP_LOAD_VAR_SQUARE;
    case CXPR_OP_LOAD_PARAM: return CXPR_IR_VIEW_OP_LOAD_PARAM;
    case CXPR_OP_LOAD_PARAM_SQUARE: return CXPR_IR_VIEW_OP_LOAD_PARAM_SQUARE;
    case CXPR_OP_LOAD_FIELD: return CXPR_IR_VIEW_OP_LOAD_FIELD;
    case CXPR_OP_LOAD_FIELD_SQUARE: return CXPR_IR_VIEW_OP_LOAD_FIELD_SQUARE;
    case CXPR_OP_LOAD_NAMED_FIELD: return CXPR_IR_VIEW_OP_LOAD_NAMED_FIELD;
    case CXPR_OP_LOAD_CHAIN: return CXPR_IR_VIEW_OP_LOAD_CHAIN;
    case CXPR_OP_ADD: return CXPR_IR_VIEW_OP_ADD;
    case CXPR_OP_SUB: return CXPR_IR_VIEW_OP_SUB;
    case CXPR_OP_MUL: return CXPR_IR_VIEW_OP_MUL;
    case CXPR_OP_SQUARE: return CXPR_IR_VIEW_OP_SQUARE;
    case CXPR_OP_DIV: return CXPR_IR_VIEW_OP_DIV;
    case CXPR_OP_MOD: return CXPR_IR_VIEW_OP_MOD;
    case CXPR_OP_CMP_EQ: return CXPR_IR_VIEW_OP_CMP_EQ;
    case CXPR_OP_CMP_NEQ: return CXPR_IR_VIEW_OP_CMP_NEQ;
    case CXPR_OP_CMP_LT: return CXPR_IR_VIEW_OP_CMP_LT;
    case CXPR_OP_CMP_LTE: return CXPR_IR_VIEW_OP_CMP_LTE;
    case CXPR_OP_CMP_GT: return CXPR_IR_VIEW_OP_CMP_GT;
    case CXPR_OP_CMP_GTE: return CXPR_IR_VIEW_OP_CMP_GTE;
    case CXPR_OP_NOT: return CXPR_IR_VIEW_OP_NOT;
    case CXPR_OP_NEG: return CXPR_IR_VIEW_OP_NEG;
    case CXPR_OP_SIGN: return CXPR_IR_VIEW_OP_SIGN;
    case CXPR_OP_SQRT: return CXPR_IR_VIEW_OP_SQRT;
    case CXPR_OP_ABS: return CXPR_IR_VIEW_OP_ABS;
    case CXPR_OP_FLOOR: return CXPR_IR_VIEW_OP_FLOOR;
    case CXPR_OP_CEIL: return CXPR_IR_VIEW_OP_CEIL;
    case CXPR_OP_ROUND: return CXPR_IR_VIEW_OP_ROUND;
    case CXPR_OP_POW: return CXPR_IR_VIEW_OP_POW;
    case CXPR_OP_CLAMP: return CXPR_IR_VIEW_OP_CLAMP;
    case CXPR_OP_CALL_PRODUCER: return CXPR_IR_VIEW_OP_CALL_PRODUCER;
    case CXPR_OP_CALL_PRODUCER_CONST: return CXPR_IR_VIEW_OP_CALL_PRODUCER_CONST;
    case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
        return CXPR_IR_VIEW_OP_CALL_PRODUCER_CONST_FIELD;
    case CXPR_OP_GET_FIELD: return CXPR_IR_VIEW_OP_GET_FIELD;
    case CXPR_OP_CALL_UNARY: return CXPR_IR_VIEW_OP_CALL_UNARY;
    case CXPR_OP_CALL_BINARY: return CXPR_IR_VIEW_OP_CALL_BINARY;
    case CXPR_OP_CALL_TERNARY: return CXPR_IR_VIEW_OP_CALL_TERNARY;
    case CXPR_OP_CALL_FUNC: return CXPR_IR_VIEW_OP_CALL_FUNC;
    case CXPR_OP_CALL_DEFINED: return CXPR_IR_VIEW_OP_CALL_DEFINED;
    case CXPR_OP_CALL_AST: return CXPR_IR_VIEW_OP_CALL_AST;
    case CXPR_OP_JUMP: return CXPR_IR_VIEW_OP_JUMP;
    case CXPR_OP_JUMP_IF_FALSE: return CXPR_IR_VIEW_OP_JUMP_IF_FALSE;
    case CXPR_OP_JUMP_IF_TRUE: return CXPR_IR_VIEW_OP_JUMP_IF_TRUE;
    case CXPR_OP_LOOKBACK_PUSH: return CXPR_IR_VIEW_OP_LOOKBACK_PUSH;
    case CXPR_OP_LOOKBACK_POP: return CXPR_IR_VIEW_OP_LOOKBACK_POP;
    case CXPR_OP_LOOKBACK_RESOLVE: return CXPR_IR_VIEW_OP_LOOKBACK_RESOLVE;
    case CXPR_OP_STORE_LOCAL: return CXPR_IR_VIEW_OP_STORE_LOCAL;
    case CXPR_OP_RETURN: return CXPR_IR_VIEW_OP_RETURN;
    default: return CXPR_IR_VIEW_OP_UNKNOWN;
    }
}

static bool cxpr_ir_view_opcode_has_hash(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
    case CXPR_OP_LOAD_FIELD:
    case CXPR_OP_LOAD_FIELD_SQUARE:
    case CXPR_OP_LOAD_NAMED_FIELD:
    case CXPR_OP_LOAD_CHAIN:
        return true;
    default:
        return false;
    }
}

static bool cxpr_ir_view_opcode_has_index(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_JUMP:
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
    case CXPR_OP_LOOKBACK_PUSH:
    case CXPR_OP_LOOKBACK_RESOLVE:
    case CXPR_OP_STORE_LOCAL:
        return true;
    default:
        return false;
    }
}

static bool cxpr_ir_view_opcode_has_arg_count(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_BUILD_ARRAY:
    case CXPR_OP_CALL_PRODUCER:
    case CXPR_OP_CALL_PRODUCER_CONST:
    case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
    case CXPR_OP_CALL_UNARY:
    case CXPR_OP_CALL_BINARY:
    case CXPR_OP_CALL_TERNARY:
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        return true;
    default:
        return false;
    }
}

size_t cxpr_expr_compiled_ir_count(const cxpr_expr_compiled* program) {
    return program ? program->ir.count : 0;
}

bool cxpr_expr_compiled_ir_instruction(const cxpr_expr_compiled* program,
                           size_t index,
                           cxpr_ir_instruction* out) {
    if (out) *out = (cxpr_ir_instruction){ .op = CXPR_IR_VIEW_OP_UNKNOWN };
    if (!program || !out || index >= program->ir.count) return false;

    const cxpr_ir_instr* instr = &program->ir.code[index];
    out->op = cxpr_ir_view_map_opcode(instr->op);
    out->name = instr->name;
    out->aux_name = instr->aux_name;
    out->func_name = instr->func ? instr->func->name : NULL;

    if (instr->op == CXPR_OP_PUSH_CONST || instr->op == CXPR_OP_PUSH_BOOL) {
        out->value = instr->value;
        out->has_value = true;
    }

    if (cxpr_ir_view_opcode_has_hash(instr->op)) {
        out->hash = instr->hash;
        out->has_hash = true;
    }

    if (cxpr_ir_view_opcode_has_index(instr->op)) {
        out->index = instr->index;
        out->has_index = true;
    }

    if (cxpr_ir_view_opcode_has_arg_count(instr->op)) {
        out->arg_count = instr->index;
        out->has_arg_count = true;
    }

    if (instr->op == CXPR_OP_CALL_PRODUCER_CONST_FIELD && instr->payload) {
        out->number_args = (const double*)instr->payload;
        out->number_arg_count = instr->index;
        out->has_number_args = true;
    }

    return true;
}

cxpr_ir_result_kind cxpr_expr_compiled_ir_result_kind(const cxpr_expr_compiled* program) {
    if (!program) return CXPR_IR_VIEW_RESULT_UNKNOWN;
    switch (program->ir.fast_result_kind) {
    case CXPR_IR_RESULT_DOUBLE: return CXPR_IR_VIEW_RESULT_NUMBER;
    case CXPR_IR_RESULT_BOOL: return CXPR_IR_VIEW_RESULT_BOOL;
    default: return CXPR_IR_VIEW_RESULT_UNKNOWN;
    }
}

const char* cxpr_ir_opcode_name(cxpr_ir_opcode op) {
    switch (op) {
    case CXPR_IR_VIEW_OP_PUSH_CONST: return "PUSH_CONST";
    case CXPR_IR_VIEW_OP_PUSH_BOOL: return "PUSH_BOOL";
    case CXPR_IR_VIEW_OP_PUSH_STRING: return "PUSH_STRING";
    case CXPR_IR_VIEW_OP_BUILD_ARRAY: return "BUILD_ARRAY";
    case CXPR_IR_VIEW_OP_LOAD_LOCAL: return "LOAD_LOCAL";
    case CXPR_IR_VIEW_OP_LOAD_LOCAL_SQUARE: return "LOAD_LOCAL_SQUARE";
    case CXPR_IR_VIEW_OP_LOAD_VAR: return "LOAD_VAR";
    case CXPR_IR_VIEW_OP_LOAD_VAR_SQUARE: return "LOAD_VAR_SQUARE";
    case CXPR_IR_VIEW_OP_LOAD_PARAM: return "LOAD_PARAM";
    case CXPR_IR_VIEW_OP_LOAD_PARAM_SQUARE: return "LOAD_PARAM_SQUARE";
    case CXPR_IR_VIEW_OP_LOAD_FIELD: return "LOAD_FIELD";
    case CXPR_IR_VIEW_OP_LOAD_FIELD_SQUARE: return "LOAD_FIELD_SQUARE";
    case CXPR_IR_VIEW_OP_LOAD_NAMED_FIELD: return "LOAD_NAMED_FIELD";
    case CXPR_IR_VIEW_OP_LOAD_CHAIN: return "LOAD_CHAIN";
    case CXPR_IR_VIEW_OP_ADD: return "ADD";
    case CXPR_IR_VIEW_OP_SUB: return "SUB";
    case CXPR_IR_VIEW_OP_MUL: return "MUL";
    case CXPR_IR_VIEW_OP_SQUARE: return "SQUARE";
    case CXPR_IR_VIEW_OP_DIV: return "DIV";
    case CXPR_IR_VIEW_OP_MOD: return "MOD";
    case CXPR_IR_VIEW_OP_CMP_EQ: return "CMP_EQ";
    case CXPR_IR_VIEW_OP_CMP_NEQ: return "CMP_NEQ";
    case CXPR_IR_VIEW_OP_CMP_LT: return "CMP_LT";
    case CXPR_IR_VIEW_OP_CMP_LTE: return "CMP_LTE";
    case CXPR_IR_VIEW_OP_CMP_GT: return "CMP_GT";
    case CXPR_IR_VIEW_OP_CMP_GTE: return "CMP_GTE";
    case CXPR_IR_VIEW_OP_NOT: return "NOT";
    case CXPR_IR_VIEW_OP_NEG: return "NEG";
    case CXPR_IR_VIEW_OP_SIGN: return "SIGN";
    case CXPR_IR_VIEW_OP_SQRT: return "SQRT";
    case CXPR_IR_VIEW_OP_ABS: return "ABS";
    case CXPR_IR_VIEW_OP_FLOOR: return "FLOOR";
    case CXPR_IR_VIEW_OP_CEIL: return "CEIL";
    case CXPR_IR_VIEW_OP_ROUND: return "ROUND";
    case CXPR_IR_VIEW_OP_POW: return "POW";
    case CXPR_IR_VIEW_OP_CLAMP: return "CLAMP";
    case CXPR_IR_VIEW_OP_CALL_PRODUCER: return "CALL_PRODUCER";
    case CXPR_IR_VIEW_OP_CALL_PRODUCER_CONST: return "CALL_PRODUCER_CONST";
    case CXPR_IR_VIEW_OP_CALL_PRODUCER_CONST_FIELD:
        return "CALL_PRODUCER_CONST_FIELD";
    case CXPR_IR_VIEW_OP_GET_FIELD: return "GET_FIELD";
    case CXPR_IR_VIEW_OP_CALL_UNARY: return "CALL_UNARY";
    case CXPR_IR_VIEW_OP_CALL_BINARY: return "CALL_BINARY";
    case CXPR_IR_VIEW_OP_CALL_TERNARY: return "CALL_TERNARY";
    case CXPR_IR_VIEW_OP_CALL_FUNC: return "CALL_FUNC";
    case CXPR_IR_VIEW_OP_CALL_DEFINED: return "CALL_DEFINED";
    case CXPR_IR_VIEW_OP_CALL_AST: return "CALL_AST";
    case CXPR_IR_VIEW_OP_JUMP: return "JUMP";
    case CXPR_IR_VIEW_OP_JUMP_IF_FALSE: return "JUMP_IF_FALSE";
    case CXPR_IR_VIEW_OP_JUMP_IF_TRUE: return "JUMP_IF_TRUE";
    case CXPR_IR_VIEW_OP_LOOKBACK_PUSH: return "LOOKBACK_PUSH";
    case CXPR_IR_VIEW_OP_LOOKBACK_POP: return "LOOKBACK_POP";
    case CXPR_IR_VIEW_OP_LOOKBACK_RESOLVE: return "LOOKBACK_RESOLVE";
    case CXPR_IR_VIEW_OP_STORE_LOCAL: return "STORE_LOCAL";
    case CXPR_IR_VIEW_OP_RETURN: return "RETURN";
    case CXPR_IR_VIEW_OP_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}
