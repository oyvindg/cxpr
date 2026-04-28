/**
 * @file shared.c
 * @brief Shared IR compilation helpers.
 */

#include "internal.h"

size_t cxpr_ir_local_index(const char* name, const char* const* local_names,
                           size_t local_count) {
    size_t i;
    if (!name || !local_names) return (size_t)-1;
    for (i = 0; i < local_count; ++i) {
        if (local_names[i] && strcmp(local_names[i], name) == 0) return i;
    }
    return (size_t)-1;
}

const cxpr_ast* cxpr_ir_subst_lookup(const cxpr_ir_subst_frame* frame, const char* name,
                                     const cxpr_ir_subst_frame** owner) {
    size_t i;
    for (; frame; frame = frame->parent) {
        for (i = 0; i < frame->count; ++i) {
            if (frame->names[i] && strcmp(frame->names[i], name) == 0) {
                if (owner) *owner = frame;
                return frame->args[i];
            }
        }
    }
    if (owner) *owner = NULL;
    return NULL;
}

bool cxpr_ir_emit_leaf_load(const cxpr_ast* ast, cxpr_ir_program* program,
                            const char* const* local_names, size_t local_count,
                            const cxpr_ir_subst_frame* subst, bool square,
                            cxpr_error* err) {
    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER: {
        const cxpr_ir_subst_frame* owner = NULL;
        const cxpr_ast* mapped = cxpr_ir_subst_lookup(subst, ast->data.identifier.name, &owner);
        if (mapped) {
            return cxpr_ir_emit_leaf_load(mapped, program, local_names, local_count,
                                          owner ? owner->parent : NULL, square, err);
        }
        {
            const size_t local_index =
                cxpr_ir_local_index(ast->data.identifier.name, local_names, local_count);
            if (local_index != (size_t)-1) {
                return cxpr_ir_emit(program,
                                    (cxpr_ir_instr){
                                        .op = square ? CXPR_OP_LOAD_LOCAL_SQUARE : CXPR_OP_LOAD_LOCAL,
                                        .index = local_index,
                                    },
                                    err);
            }
        }
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_VAR_SQUARE : CXPR_OP_LOAD_VAR,
                                .name = ast->data.identifier.name,
                                .hash = cxpr_hash_string(ast->data.identifier.name),
                            },
                            err);
    }

    case CXPR_NODE_VARIABLE:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_PARAM_SQUARE : CXPR_OP_LOAD_PARAM,
                                .name = ast->data.variable.name,
                                .hash = cxpr_hash_string(ast->data.variable.name),
                            },
                            err);

    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_ir_emit(program,
                            (cxpr_ir_instr){
                                .op = square ? CXPR_OP_LOAD_FIELD_SQUARE : CXPR_OP_LOAD_FIELD,
                                .name = ast->data.field_access.full_key,
                                .hash = cxpr_hash_string(ast->data.field_access.full_key),
                            },
                            err);

    default:
        return false;
    }
}

bool cxpr_ir_ast_contains_string_literal(const cxpr_ast* ast) {
    size_t i;

    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_STRING:
        return true;

    case CXPR_NODE_BINARY_OP:
        return cxpr_ir_ast_contains_string_literal(ast->data.binary_op.left) ||
               cxpr_ir_ast_contains_string_literal(ast->data.binary_op.right);

    case CXPR_NODE_UNARY_OP:
        return cxpr_ir_ast_contains_string_literal(ast->data.unary_op.operand);

    case CXPR_NODE_FUNCTION_CALL:
        for (i = 0; i < ast->data.function_call.argc; ++i) {
            if (cxpr_ir_ast_contains_string_literal(ast->data.function_call.args[i])) {
                return true;
            }
        }
        return false;

    case CXPR_NODE_PRODUCER_ACCESS:
        for (i = 0; i < ast->data.producer_access.argc; ++i) {
            if (cxpr_ir_ast_contains_string_literal(ast->data.producer_access.args[i])) {
                return true;
            }
        }
        return false;

    case CXPR_NODE_LOOKBACK:
        return cxpr_ir_ast_contains_string_literal(ast->data.lookback.target) ||
               cxpr_ir_ast_contains_string_literal(ast->data.lookback.index);

    case CXPR_NODE_TERNARY:
        return cxpr_ir_ast_contains_string_literal(ast->data.ternary.condition) ||
               cxpr_ir_ast_contains_string_literal(ast->data.ternary.true_branch) ||
               cxpr_ir_ast_contains_string_literal(ast->data.ternary.false_branch);

    default:
        return false;
    }
}

bool cxpr_ir_arg_needs_overlay_passthrough(const cxpr_ast* ast) {
    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_FUNCTION_CALL:
    case CXPR_NODE_PRODUCER_ACCESS:
    case CXPR_NODE_LOOKBACK:
        return true;
    case CXPR_NODE_BINARY_OP:
        return cxpr_ir_arg_needs_overlay_passthrough(ast->data.binary_op.left) ||
               cxpr_ir_arg_needs_overlay_passthrough(ast->data.binary_op.right);
    case CXPR_NODE_UNARY_OP:
        return cxpr_ir_arg_needs_overlay_passthrough(ast->data.unary_op.operand);
    case CXPR_NODE_TERNARY:
        return cxpr_ir_arg_needs_overlay_passthrough(ast->data.ternary.condition) ||
               cxpr_ir_arg_needs_overlay_passthrough(ast->data.ternary.true_branch) ||
               cxpr_ir_arg_needs_overlay_passthrough(ast->data.ternary.false_branch);
    default:
        return false;
    }
}

bool cxpr_ir_runtime_call_needs_overlay_passthrough(const cxpr_ast* ast) {
    size_t i;

    if (!ast) return false;

    switch (ast->type) {
    case CXPR_NODE_FUNCTION_CALL:
        for (i = 0; i < ast->data.function_call.argc; ++i) {
            if (cxpr_ir_arg_needs_overlay_passthrough(ast->data.function_call.args[i])) {
                return true;
            }
        }
        return false;

    case CXPR_NODE_PRODUCER_ACCESS:
        for (i = 0; i < ast->data.producer_access.argc; ++i) {
            if (cxpr_ir_arg_needs_overlay_passthrough(ast->data.producer_access.args[i])) {
                return true;
            }
        }
        return false;

    default:
        return false;
    }
}
