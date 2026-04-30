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

typedef struct {
    size_t numbers;
    size_t bools;
} cxpr_ir_fast_stack_state;

static bool cxpr_ir_bool_fast_stack_effect(const cxpr_ir_instr* instr,
                                           cxpr_ir_fast_stack_state* state) {
    if (!instr || !state) return false;

    switch (instr->op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
        state->numbers++;
        return true;

    case CXPR_OP_PUSH_BOOL:
        state->bools++;
        return true;

    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_POW:
        if (state->numbers < 2u) return false;
        state->numbers--;
        return true;

    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
        if (state->numbers < 2u) return false;
        state->numbers -= 2u;
        state->bools++;
        return true;

    case CXPR_OP_SQUARE:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
    case CXPR_OP_CALL_UNARY:
        return state->numbers >= 1u;

    case CXPR_OP_NOT:
        return state->bools >= 1u;

    case CXPR_OP_CLAMP:
    case CXPR_OP_CALL_TERNARY:
        if (state->numbers < 3u) return false;
        state->numbers -= 2u;
        return true;

    case CXPR_OP_CALL_BINARY:
        if (state->numbers < 2u) return false;
        state->numbers--;
        return true;

    case CXPR_OP_CALL_FUNC:
        if (instr->index > 32u || state->numbers < instr->index) return false;
        state->numbers -= instr->index;
        if (instr->func && instr->func->value_func && instr->func->has_return_type &&
            instr->func->return_type == CXPR_VALUE_BOOL) {
            state->bools++;
            return true;
        }
        if (instr->func && !instr->func->typed_func) {
            state->numbers++;
            return true;
        }
        return false;

    case CXPR_OP_CALL_DEFINED:
        if (instr->index > 32u || state->numbers < instr->index) return false;
        state->numbers -= instr->index;
        if (instr->func && instr->func->has_return_type &&
            instr->func->return_type == CXPR_VALUE_BOOL) {
            state->bools++;
            return true;
        }
        if (instr->func && instr->func->has_return_type &&
            instr->func->return_type == CXPR_VALUE_NUMBER) {
            state->numbers++;
            return true;
        }
        return false;

    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
        if (state->bools < 1u) return false;
        state->bools--;
        return true;

    case CXPR_OP_JUMP:
        return true;

    case CXPR_OP_RETURN:
        if (state->numbers != 0u || state->bools != 1u) return false;
        state->bools = 0u;
        return true;

    default:
        return false;
    }
}

static bool cxpr_ir_bool_fast_enqueue(size_t target,
                                      cxpr_ir_fast_stack_state state,
                                      const cxpr_ir_program* program,
                                      cxpr_ir_fast_stack_state* states,
                                      bool* seen,
                                      size_t* worklist,
                                      size_t* work_count) {
    if (target >= program->count || *work_count >= 256u) return false;
    if (!seen[target]) {
        seen[target] = true;
        states[target] = state;
        worklist[(*work_count)++] = target;
        return true;
    }
    return states[target].numbers == state.numbers && states[target].bools == state.bools;
}

bool cxpr_ir_validate_bool_fast_program(const cxpr_ir_program* program) {
    cxpr_ir_fast_stack_state states[256];
    bool seen[256] = {0};
    size_t worklist[256];
    size_t work_count = 0u;

    if (!program || !program->code || program->count == 0u || program->count > 256u) {
        return false;
    }

    states[0] = (cxpr_ir_fast_stack_state){0u, 0u};
    seen[0] = true;
    worklist[work_count++] = 0u;

    while (work_count > 0u) {
        size_t ip = worklist[--work_count];
        const cxpr_ir_instr* instr = &program->code[ip];
        cxpr_ir_fast_stack_state next = states[ip];

        if (!cxpr_ir_bool_fast_stack_effect(instr, &next)) return false;
        if (next.numbers > CXPR_IR_STACK_CAPACITY || next.bools > CXPR_IR_STACK_CAPACITY) {
            return false;
        }

        if (instr->op == CXPR_OP_RETURN) continue;

        if (instr->op == CXPR_OP_JUMP) {
            if (!cxpr_ir_bool_fast_enqueue(instr->index, next, program, states, seen,
                                           worklist, &work_count)) {
                return false;
            }
            continue;
        }

        if (instr->op == CXPR_OP_JUMP_IF_FALSE || instr->op == CXPR_OP_JUMP_IF_TRUE) {
            if (!cxpr_ir_bool_fast_enqueue(instr->index, next, program, states, seen,
                                           worklist, &work_count)) {
                return false;
            }
        }

        if (ip + 1u >= program->count) return false;
        if (!cxpr_ir_bool_fast_enqueue(ip + 1u, next, program, states, seen,
                                       worklist, &work_count)) {
            return false;
        }
    }

    return true;
}
