/**
 * @file infer.c
 * @brief IR fast-path result inference.
 */

#include "internal.h"

unsigned char cxpr_ir_infer_fast_result_kind(const cxpr_ast* ast, const cxpr_registry* reg,
                                             size_t depth) {
    unsigned char left_kind;
    unsigned char right_kind;
    unsigned char cond_kind;
    cxpr_func_entry* entry;
    size_t i;

    if (!ast || depth > CXPR_IR_INFER_DEPTH_LIMIT) return CXPR_IR_RESULT_UNKNOWN;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
        return CXPR_IR_RESULT_DOUBLE;

    case CXPR_NODE_BOOL:
        return CXPR_IR_RESULT_BOOL;

    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
    case CXPR_NODE_LOOKBACK:
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_UNARY_OP:
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.unary_op.operand, reg, depth + 1);
        if (ast->data.unary_op.op == CXPR_TOK_MINUS &&
            left_kind == CXPR_IR_RESULT_DOUBLE) {
            return CXPR_IR_RESULT_DOUBLE;
        }
        if (ast->data.unary_op.op == CXPR_TOK_NOT &&
            left_kind == CXPR_IR_RESULT_BOOL) {
            return CXPR_IR_RESULT_BOOL;
        }
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_BINARY_OP:
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.binary_op.left, reg, depth + 1);
        right_kind = cxpr_ir_infer_fast_result_kind(ast->data.binary_op.right, reg, depth + 1);
        if (!left_kind || !right_kind) return CXPR_IR_RESULT_UNKNOWN;
        switch (ast->data.binary_op.op) {
        case CXPR_TOK_PLUS:
        case CXPR_TOK_MINUS:
        case CXPR_TOK_STAR:
        case CXPR_TOK_SLASH:
        case CXPR_TOK_PERCENT:
        case CXPR_TOK_POWER:
            return (left_kind == CXPR_IR_RESULT_DOUBLE &&
                    right_kind == CXPR_IR_RESULT_DOUBLE)
                       ? CXPR_IR_RESULT_DOUBLE
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            return (left_kind == CXPR_IR_RESULT_DOUBLE &&
                    right_kind == CXPR_IR_RESULT_DOUBLE)
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            return (left_kind == CXPR_IR_RESULT_BOOL &&
                    right_kind == CXPR_IR_RESULT_BOOL)
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
            return (left_kind == right_kind &&
                    (left_kind == CXPR_IR_RESULT_DOUBLE ||
                     left_kind == CXPR_IR_RESULT_BOOL))
                       ? CXPR_IR_RESULT_BOOL
                       : CXPR_IR_RESULT_UNKNOWN;
        default:
            return CXPR_IR_RESULT_UNKNOWN;
        }

    case CXPR_NODE_TERNARY:
        cond_kind = cxpr_ir_infer_fast_result_kind(ast->data.ternary.condition, reg, depth + 1);
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.ternary.true_branch, reg, depth + 1);
        right_kind =
            cxpr_ir_infer_fast_result_kind(ast->data.ternary.false_branch, reg, depth + 1);
        if (cond_kind == CXPR_IR_RESULT_BOOL && left_kind == right_kind &&
            (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_BOOL)) {
            return left_kind;
        }
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_FUNCTION_CALL:
        if (strcmp(ast->data.function_call.name, "if") == 0 &&
            ast->data.function_call.argc == 3) {
            cond_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[0], reg, depth + 1);
            left_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[1], reg, depth + 1);
            right_kind =
                cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[2], reg, depth + 1);
            if (cond_kind == CXPR_IR_RESULT_BOOL && left_kind == right_kind &&
                (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_BOOL)) {
                return left_kind;
            }
            return CXPR_IR_RESULT_UNKNOWN;
        }

        entry = cxpr_registry_find(reg, ast->data.function_call.name);
        if (!entry || entry->ast_func || (entry->struct_fields && !entry->struct_producer) ||
            (entry->struct_producer && !entry->sync_func)) {
            return CXPR_IR_RESULT_UNKNOWN;
        }

        if (entry->typed_func) {
            return CXPR_IR_RESULT_UNKNOWN;
        }

        for (i = 0; i < ast->data.function_call.argc; ++i) {
            if (cxpr_ir_infer_fast_result_kind(ast->data.function_call.args[i], reg, depth + 1) !=
                CXPR_IR_RESULT_DOUBLE) {
                return CXPR_IR_RESULT_UNKNOWN;
            }
        }

        if (entry->sync_func && !entry->value_func && !entry->defined_body) return CXPR_IR_RESULT_DOUBLE;
        if (entry->value_func && entry->has_return_type) {
            return entry->return_type == CXPR_VALUE_BOOL ? CXPR_IR_RESULT_BOOL :
                   entry->return_type == CXPR_VALUE_NUMBER ? CXPR_IR_RESULT_DOUBLE :
                   CXPR_IR_RESULT_UNKNOWN;
        }
        if (entry->defined_body && cxpr_ir_defined_is_scalar_only(entry)) {
            return cxpr_ir_infer_fast_result_kind(entry->defined_body, reg, depth + 1);
        }
        return CXPR_IR_RESULT_UNKNOWN;

    default:
        return CXPR_IR_RESULT_UNKNOWN;
    }
}
