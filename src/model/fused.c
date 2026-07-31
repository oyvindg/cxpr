#include "model/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

size_t cxpr_model_fused_slot_find(char* const* names, size_t count,
                                  const char* name) {
    if (!names || !name) return (size_t)-1;
    for (size_t i = 0; i < count; ++i) {
        if (cxpr_model_names_match(names[i], name)) return i;
    }
    return (size_t)-1;
}

static bool cxpr_model_fused_slot_add(cxpr_model_compiled* program,
                                      const char* name,
                                      size_t* out_slot,
                                      cxpr_error* err) {
    char** grown_names;
    unsigned long* grown_hashes;
    size_t existing;

    if (!program || !name) return false;
    existing = cxpr_model_fused_slot_find(
        program->fused_slot_names, program->fused_slot_count, name);
    if (existing != (size_t)-1) {
        if (out_slot) *out_slot = existing;
        return true;
    }

    grown_names = (char**)realloc(
        program->fused_slot_names, (program->fused_slot_count + 1u) * sizeof(char*));
    if (!grown_names) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    program->fused_slot_names = grown_names;
    grown_hashes = (unsigned long*)realloc(
        program->fused_slot_hashes,
        (program->fused_slot_count + 1u) * sizeof(unsigned long));
    if (!grown_hashes) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    program->fused_slot_hashes = grown_hashes;
    program->fused_slot_names[program->fused_slot_count] = cxpr_strdup(name);
    if (!program->fused_slot_names[program->fused_slot_count]) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    program->fused_slot_hashes[program->fused_slot_count] = cxpr_hash_string(name);
    if (out_slot) *out_slot = program->fused_slot_count;
    program->fused_slot_count++;
    return true;
}

static bool cxpr_model_slot_ref_append(cxpr_model_slot_ref** refs,
                                       size_t* count,
                                       const char* name,
                                       size_t slot,
                                       cxpr_model_result_kind result_kind,
                                       cxpr_error* err) {
    cxpr_model_slot_ref* grown;
    if (!refs || !count || !name) return false;
    grown = (cxpr_model_slot_ref*)realloc(
        *refs, (*count + 1u) * sizeof(cxpr_model_slot_ref));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    *refs = grown;
    (*refs)[*count].name = cxpr_strdup(name);
    (*refs)[*count].hash = cxpr_hash_string(name);
    (*refs)[*count].slot = slot;
    (*refs)[*count].result_kind = result_kind;
    if (!(*refs)[*count].name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    (*count)++;
    return true;
}

static cxpr_model_result_kind cxpr_model_compiled_symbol_result_kind(
    const cxpr_model_compiled* program,
    const char* name) {
    if (!program || !name) return CXPR_MODEL_RESULT_UNKNOWN;
    for (size_t i = 0; i < program->binding_count; ++i) {
        if (cxpr_model_names_match(program->bindings[i].name, name)) {
            return program->bindings[i].result_kind;
        }
    }
    for (size_t i = 0; i < program->state_default_count; ++i) {
        if (cxpr_model_names_match(program->state_defaults[i].name, name)) {
            return program->state_defaults[i].result_kind;
        }
    }
    for (size_t i = 0; i < program->constant_count; ++i) {
        if (cxpr_model_names_match(program->constants[i].name, name)) {
            return program->constants[i].result_kind;
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static bool cxpr_model_state_commit_append(cxpr_model_compiled* program,
                                           size_t state_slot,
                                           size_t update_slot,
                                           cxpr_error* err) {
    cxpr_model_state_commit* grown;
    grown = (cxpr_model_state_commit*)realloc(
        program->fused_commits,
        (program->fused_commit_count + 1u) * sizeof(cxpr_model_state_commit));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return false;
    }
    program->fused_commits = grown;
    program->fused_commits[program->fused_commit_count].state_slot = state_slot;
    program->fused_commits[program->fused_commit_count].update_slot = update_slot;
    program->fused_commit_count++;
    return true;
}

static bool cxpr_model_fused_slot_is_committed_state(const cxpr_model_compiled* program,
                                                     size_t slot) {
    if (!program) return false;
    for (size_t i = 0; i < program->fused_commit_count; ++i) {
        if (program->fused_commits[i].state_slot == slot) return true;
    }
    return false;
}

static bool cxpr_model_fused_scalar_opcode_supported(cxpr_opcode op) {
    switch (op) {
    case CXPR_OP_PUSH_CONST:
    case CXPR_OP_PUSH_BOOL:
    case CXPR_OP_LOAD_LOCAL:
    case CXPR_OP_LOAD_LOCAL_SQUARE:
    case CXPR_OP_LOAD_VAR:
    case CXPR_OP_LOAD_VAR_SQUARE:
    case CXPR_OP_LOAD_PARAM:
    case CXPR_OP_LOAD_PARAM_SQUARE:
    case CXPR_OP_ADD:
    case CXPR_OP_SUB:
    case CXPR_OP_MUL:
    case CXPR_OP_SQUARE:
    case CXPR_OP_DIV:
    case CXPR_OP_MOD:
    case CXPR_OP_CMP_EQ:
    case CXPR_OP_CMP_NEQ:
    case CXPR_OP_CMP_LT:
    case CXPR_OP_CMP_LTE:
    case CXPR_OP_CMP_GT:
    case CXPR_OP_CMP_GTE:
    case CXPR_OP_NOT:
    case CXPR_OP_NEG:
    case CXPR_OP_SIGN:
    case CXPR_OP_SQRT:
    case CXPR_OP_ABS:
    case CXPR_OP_FLOOR:
    case CXPR_OP_CEIL:
    case CXPR_OP_ROUND:
    case CXPR_OP_POW:
    case CXPR_OP_CLAMP:
    case CXPR_OP_CALL_UNARY:
    case CXPR_OP_CALL_BINARY:
    case CXPR_OP_CALL_TERNARY:
    case CXPR_OP_CALL_FUNC:
    case CXPR_OP_CALL_DEFINED:
    case CXPR_OP_JUMP:
    case CXPR_OP_JUMP_IF_FALSE:
    case CXPR_OP_JUMP_IF_TRUE:
    case CXPR_OP_STORE_LOCAL:
    case CXPR_OP_RETURN:
        return true;
    default:
        return false;
    }
}

static bool cxpr_model_fused_ir_scalar_supported(const cxpr_ir_program* ir,
                                                 const char** disabled_opcode) {
    if (disabled_opcode) *disabled_opcode = NULL;
    if (!ir || !ir->code) return false;
    for (size_t i = 0; i < ir->count; ++i) {
        if (!cxpr_model_fused_scalar_opcode_supported(ir->code[i].op)) {
            if (disabled_opcode) *disabled_opcode = cxpr_ir_internal_opcode_name(ir->code[i].op);
            return false;
        }
    }
    return true;
}

static bool cxpr_model_fused_ast_supported(const cxpr_expr_ast* ast,
                                           const cxpr_registry* reg) {
    if (!ast) return true;
    switch (ast->type) {
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (!cxpr_model_fused_ast_supported(ast->data.record.field_values[i], reg)) return false;
        }
        return true;
    case CXPR_NODE_ARRAY:
        for (size_t i = 0u; i < ast->data.array.count; ++i) {
            if (!cxpr_model_fused_ast_supported(ast->data.array.elements[i], reg)) return false;
        }
        return true;
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_fused_ast_supported(ast->data.binary_op.left, reg) &&
               cxpr_model_fused_ast_supported(ast->data.binary_op.right, reg);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_fused_ast_supported(ast->data.unary_op.operand, reg);
    case CXPR_NODE_FUNCTION_CALL: {
        for (size_t i = 0u; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_model_fused_ast_supported(ast->data.function_call.args[i], reg)) return false;
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_model_fused_ast_supported(ast->data.producer_access.args[i], reg)) return false;
        }
        return true;
    case CXPR_NODE_INDEX:
        return cxpr_model_fused_ast_supported(ast->data.index.target, reg) &&
               cxpr_model_fused_ast_supported(ast->data.index.index, reg);
    case CXPR_NODE_TERNARY:
        return cxpr_model_fused_ast_supported(ast->data.ternary.condition, reg) &&
               cxpr_model_fused_ast_supported(ast->data.ternary.true_branch, reg) &&
               cxpr_model_fused_ast_supported(ast->data.ternary.false_branch, reg);
    default:
        return true;
    }
}

bool cxpr_model_try_compile_fused_ir(cxpr_model_compiled* program,
                                     const cxpr_model* model,
                                     const cxpr_registry* reg,
                                     cxpr_error* err) {
    const cxpr_registry* compile_reg = program->registry ? program->registry : reg;

    if (!program || !model) return false;

    for (size_t i = 0; i < model->input_count; ++i) {
        size_t slot = 0u;
        if (!cxpr_model_fused_slot_add(program, model->inputs[i], &slot, err) ||
            !cxpr_model_slot_ref_append(&program->fused_inputs,
                                        &program->fused_input_count,
                                        model->inputs[i],
                                        slot,
                                        CXPR_MODEL_RESULT_NUMBER,
                                        err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if (!cxpr_model_fused_ast_supported(model->bindings[i].expr, compile_reg)) {
            cxpr_model_fused_program_clear(program);
            return true;
        }
        if (!cxpr_model_fused_slot_add(program, model->bindings[i].name, NULL, err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        size_t slot = 0u;
        size_t state_slot = 0u;
        char hidden_name[256];
        if (model->bindings[i].kind != CXPR_MODEL_BINDING_STATE_UPDATE) continue;
        if ((size_t)snprintf(hidden_name, sizeof(hidden_name), "__state_update_%s",
                             model->bindings[i].name) >= sizeof(hidden_name)) {
            cxpr_model_fused_program_clear(program);
            return true;
        }
        if (!cxpr_model_fused_slot_add(program, hidden_name, &slot, err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
        state_slot = cxpr_model_fused_slot_find(
            program->fused_slot_names, program->fused_slot_count, model->bindings[i].name);
        if (state_slot == (size_t)-1 ||
            !cxpr_model_state_commit_append(program, state_slot, slot, err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
    }

    for (size_t i = 0; i < program->output_count; ++i) {
        size_t slot = cxpr_model_fused_slot_find(
            program->fused_slot_names, program->fused_slot_count, program->outputs[i]);
        cxpr_model_result_kind result_kind;
        if (slot == (size_t)-1) {
            cxpr_model_fused_program_clear(program);
            return true;
        }
        result_kind = cxpr_model_compiled_symbol_result_kind(program, program->outputs[i]);
        if (!cxpr_model_slot_ref_append(&program->fused_outputs,
                                        &program->fused_output_count,
                                        program->outputs[i],
                                        slot,
                                        result_kind,
                                        err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
        if (slot != (size_t)-1 && cxpr_model_fused_slot_is_committed_state(program, slot)) {
            continue;
        }
        if (slot != (size_t)-1 &&
            !cxpr_model_slot_ref_append(&program->fused_exports,
                                        &program->fused_export_count,
                                        program->outputs[i],
                                        slot,
                                        result_kind,
                                        err)) {
            cxpr_model_fused_program_clear(program);
            return false;
        }
    }
    program->has_fused_layout = true;

    for (size_t i = 0; i < model->binding_count; ++i) {
        size_t slot;
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE) continue;
        if (!cxpr_ir_compile_node(model->bindings[i].expr,
                                  &program->fused_ir,
                                  compile_reg,
                                  (const char* const*)program->fused_slot_names,
                                  program->fused_slot_count,
                                  NULL,
                                  0u,
                                  err)) {
            cxpr_ir_program_reset(&program->fused_ir);
            program->has_fused_ir = false;
            return true;
        }
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            char hidden_name[256];
            if ((size_t)snprintf(hidden_name, sizeof(hidden_name), "__state_update_%s",
                                 model->bindings[i].name) >= sizeof(hidden_name)) {
                cxpr_ir_program_reset(&program->fused_ir);
                program->has_fused_ir = false;
                return true;
            }
            slot = cxpr_model_fused_slot_find(
                program->fused_slot_names, program->fused_slot_count, hidden_name);
        } else {
            slot = cxpr_model_fused_slot_find(
                program->fused_slot_names, program->fused_slot_count, model->bindings[i].name);
        }
        if (slot == (size_t)-1 ||
            !cxpr_ir_emit(&program->fused_ir,
                          (cxpr_ir_instr){.op = CXPR_OP_STORE_LOCAL, .index = slot},
                          err)) {
            cxpr_ir_program_reset(&program->fused_ir);
            cxpr_model_fused_program_clear(program);
            return false;
        }
    }
    if (!cxpr_ir_emit(&program->fused_ir,
                      (cxpr_ir_instr){.op = CXPR_OP_PUSH_CONST, .value = 0.0},
                      err) ||
        !cxpr_ir_emit(&program->fused_ir,
                      (cxpr_ir_instr){.op = CXPR_OP_RETURN},
                      err)) {
        cxpr_ir_program_reset(&program->fused_ir);
        cxpr_model_fused_program_clear(program);
        return false;
    }
    program->fused_ir.fast_result_kind = CXPR_IR_RESULT_DOUBLE;
    {
        const char* disabled_opcode = NULL;
        if (!cxpr_model_fused_ir_scalar_supported(&program->fused_ir, &disabled_opcode)) {
            program->fused_disabled_opcode = disabled_opcode;
            cxpr_ir_program_reset(&program->fused_ir);
            program->has_fused_ir = false;
            return true;
        }
    }
    program->fused_ir.lookup_cache =
        (cxpr_ir_lookup_cache*)calloc(program->fused_ir.count, sizeof(cxpr_ir_lookup_cache));
    if (program->fused_ir.count > 0u && !program->fused_ir.lookup_cache) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        cxpr_ir_program_reset(&program->fused_ir);
        cxpr_model_fused_program_clear(program);
        return false;
    }
    program->has_fused_ir = true;
    return true;
}
