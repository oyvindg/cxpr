/**
 * @file ir_validate.c
 * @brief IR validation helpers.
 */

#include "internal.h"

static bool cxpr_ir_scalar_stack_effect(const cxpr_ir_instr* instr, size_t* pops, size_t* pushes) {
    if (!instr || !pops || !pushes) return false;

    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        *pops = 0;
        *pushes = 1;
        return true;
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
    case CXPR_OP_CALL_BINARY:
        *pops = 2;
        *pushes = 1;
        return true;
    case CXPR_OP_CLAMP:
    case CXPR_OP_CALL_TERNARY:
        *pops = 3;
        *pushes = 1;
        return true;
    case CXPR_OP_SQUARE:
    case CXPR_OP_NOT:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
    case CXPR_OP_CALL_UNARY:
    case CXPR_OP_GET_FIELD:
        *pops = 1;
        *pushes = 1;
        return true;
    case CXPR_OP_CALL_PRODUCER_CONST_FIELD:
        if (instr->index > 32) return false;
        *pops = 0;
        *pushes = 1;
        return true;
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
        if (instr->index > 32) return false;
        *pops = instr->index;
        *pushes = 1;
        return true;
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
        *pops = 1;
        *pushes = 0;
        return true;
    case CXPR_OP_JUMP:
        *pops = 0;
        *pushes = 0;
        return true;
    case CXPR_OP_RETURN:
        *pops = 1;
        *pushes = 0;
        return true;
    default:
        return false;
    }
}

bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program) {
    size_t depths[256];
    size_t worklist[256];
    size_t work_count = 0;

    if (!program || !program->code || program->count == 0 || program->count > 256) {
        return false;
    }

    for (size_t i = 0; i < program->count; ++i) depths[i] = SIZE_MAX;
    depths[0] = 0;
    worklist[work_count++] = 0;

    while (work_count > 0) {
        size_t ip = worklist[--work_count];
        const cxpr_ir_instr* instr = &program->code[ip];
        size_t pops = 0;
        size_t pushes = 0;
        size_t depth = depths[ip];
        size_t next_depth;

        if (!cxpr_ir_scalar_stack_effect(instr, &pops, &pushes) || depth < pops) {
            return false;
        }

        next_depth = depth - pops + pushes;
        if (next_depth > CXPR_IR_STACK_CAPACITY) return false;

        if (instr->op == CXPR_OP_RETURN) {
            if (depth != 1) return false;
            continue;
        }

        if (instr->op == CXPR_OP_JUMP) {
            if (instr->index >= program->count) return false;
            if (depths[instr->index] == SIZE_MAX) {
                depths[instr->index] = next_depth;
                worklist[work_count++] = instr->index;
            } else if (depths[instr->index] != next_depth) {
                return false;
            }
            continue;
        }

        if (instr->op == CXPR_OP_JUMP_IF_FALSE || instr->op == CXPR_OP_JUMP_IF_TRUE) {
            if (instr->index >= program->count || ip + 1 >= program->count) return false;
            if (depths[instr->index] == SIZE_MAX) {
                depths[instr->index] = next_depth;
                worklist[work_count++] = instr->index;
            } else if (depths[instr->index] != next_depth) {
                return false;
            }
        }

        if (ip + 1 >= program->count) return false;
        if (depths[ip + 1] == SIZE_MAX) {
            depths[ip + 1] = next_depth;
            worklist[work_count++] = ip + 1;
        } else if (depths[ip + 1] != next_depth) {
            return false;
        }
    }

    return true;
}
