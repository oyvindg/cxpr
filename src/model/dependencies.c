/**
 * @file model/dependencies.c
 * @brief Required-binding dependency analysis for compiled models.
 */

#include "model/internal.h"

#include <stdlib.h>
#include <string.h>

static size_t cxpr_model_program_binding_index_for_name(
    const cxpr_model_program* program,
    const char* name) {
    if (!program || !name) return (size_t)-1;
    for (size_t i = 0u; i < program->binding_count; ++i) {
        if (cxpr_model_names_match(program->bindings[i].name, name)) return i;
    }
    return (size_t)-1;
}

static bool cxpr_model_program_mark_required_symbol(const cxpr_model_program* program,
                                                    const char* name,
                                                    bool* out_required,
                                                    cxpr_error* err);

static bool cxpr_model_program_mark_required_ast(const cxpr_model_program* program,
                                                 const cxpr_ast* ast,
                                                 bool* out_required,
                                                 cxpr_error* err) {
    if (!ast) return true;
    switch (cxpr_ast_type(ast)) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_identifier_name(ast), out_required, err);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_field_object(ast), out_required, err);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_model_program_mark_required_symbol(
            program, cxpr_ast_chain_segment(ast, 0u), out_required, err);
    case CXPR_NODE_BINARY_OP:
        return cxpr_model_program_mark_required_ast(program, cxpr_ast_left(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(program, cxpr_ast_right(ast), out_required, err);
    case CXPR_NODE_UNARY_OP:
        return cxpr_model_program_mark_required_ast(program, cxpr_ast_operand(ast), out_required, err);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_ast_function_argc(ast); ++i) {
            if (!cxpr_model_program_mark_required_ast(
                    program, cxpr_ast_function_arg(ast, i), out_required, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0u; i < cxpr_ast_producer_argc(ast); ++i) {
            if (!cxpr_model_program_mark_required_ast(
                    program, cxpr_ast_producer_arg(ast, i), out_required, err)) {
                return false;
            }
        }
        return true;
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_lookback_target(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_lookback_index(ast), out_required, err);
    case CXPR_NODE_TERNARY:
        return cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_condition(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_true_branch(ast), out_required, err) &&
               cxpr_model_program_mark_required_ast(
                   program, cxpr_ast_ternary_false_branch(ast), out_required, err);
    default:
        return true;
    }
}

static bool cxpr_model_program_mark_required_symbol(const cxpr_model_program* program,
                                                    const char* name,
                                                    bool* out_required,
                                                    cxpr_error* err) {
    size_t index = cxpr_model_program_binding_index_for_name(program, name);
    if (index == (size_t)-1) return true;
    if (out_required[index]) return true;
    out_required[index] = true;
    if (!program->bindings[index].ast) return true;
    return cxpr_model_program_mark_required_ast(
        program, program->bindings[index].ast, out_required, err);
}

bool cxpr_model_program_mark_required_bindings(const cxpr_model_program* program,
                                               const size_t* output_indices,
                                               size_t output_count,
                                               bool include_all_outputs,
                                               bool include_state_commits,
                                               bool include_history_captures,
                                               bool* out_required,
                                               cxpr_error* err) {
    if (!program || !out_required) return false;
    if (include_all_outputs) {
        for (size_t i = 0u; i < program->fused_output_count; ++i) {
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_outputs[i].name, out_required, err)) {
                return false;
            }
        }
    } else {
        if (output_count > 0u && !output_indices) return false;
        for (size_t out_i = 0u; out_i < output_count; ++out_i) {
            size_t i = output_indices[out_i];
            if (i >= program->fused_output_count) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model selected-output index out of range", 0, 0);
                return false;
            }
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_outputs[i].name, out_required, err)) {
                return false;
            }
        }
    }
    if (include_state_commits) {
        for (size_t i = 0u; i < program->fused_commit_count; ++i) {
            size_t slot = program->fused_commits[i].state_slot;
            if (slot >= program->fused_slot_count) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Model state commit slot out of range", 0, 0);
                return false;
            }
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->fused_slot_names[slot], out_required, err)) {
                return false;
            }
        }
    }
    if (include_history_captures) {
        for (size_t i = 0u; i < program->history_spec_count; ++i) {
            if (!cxpr_model_program_mark_required_symbol(
                    program, program->history_specs[i].name, out_required, err)) {
                return false;
            }
        }
    }
    return true;
}
