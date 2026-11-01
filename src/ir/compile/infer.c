/**
 * @file infer.c
 * @brief IR fast-path result inference.
 */

#include "internal.h"

static bool cxpr_ir_infer_numeric_selected_field(const cxpr_expr_ast* ast,
                                                 const char* field) {
    if (!ast || !field) return false;
    if (ast->type == CXPR_NODE_IDENTIFIER) return true;
    if (ast->type == CXPR_NODE_RECORD) {
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            if (strcmp(ast->data.record.field_names[i], field) == 0) return true;
        }
        return false;
    }
    if (ast->type != CXPR_NODE_BINARY_OP ||
        (ast->data.binary_op.op != CXPR_TOK_PLUS &&
         ast->data.binary_op.op != CXPR_TOK_MINUS &&
         ast->data.binary_op.op != CXPR_TOK_STAR &&
         ast->data.binary_op.op != CXPR_TOK_SLASH)) {
        return false;
    }
    const cxpr_expr_ast* left = ast->data.binary_op.left;
    const cxpr_expr_ast* right = ast->data.binary_op.right;
    const bool left_literal = left && (left->type == CXPR_NODE_NUMBER ||
                                       left->type == CXPR_NODE_INT64);
    const bool right_literal = right && (right->type == CXPR_NODE_NUMBER ||
                                         right->type == CXPR_NODE_INT64);
    if (left_literal && right_literal) return false;
    return (left_literal || cxpr_ir_infer_numeric_selected_field(left, field)) &&
           (right_literal || cxpr_ir_infer_numeric_selected_field(right, field));
}

unsigned char cxpr_ir_infer_fast_result_kind(const cxpr_expr_ast* ast, const cxpr_registry* reg,
                                             size_t depth) {
    unsigned char left_kind;
    unsigned char right_kind;
    unsigned char cond_kind;
    cxpr_func_entry* entry;
    size_t i;

    if (!ast || depth > CXPR_IR_INFER_DEPTH_LIMIT) return CXPR_IR_RESULT_UNKNOWN;

    switch (ast->type) {
    case CXPR_NODE_INT64:
        return CXPR_IR_RESULT_INT64;
    case CXPR_NODE_NUMBER:
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
        return CXPR_IR_RESULT_DOUBLE;

    case CXPR_NODE_BOOL:
        return CXPR_IR_RESULT_BOOL;

    case CXPR_NODE_FIELD_ACCESS:
        return ast->data.field_access.base &&
                       cxpr_ir_infer_numeric_selected_field(ast->data.field_access.base,
                                                            ast->data.field_access.field)
                   ? CXPR_IR_RESULT_DOUBLE : CXPR_IR_RESULT_UNKNOWN;
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
    case CXPR_NODE_INDEX:
    case CXPR_NODE_RECORD:
        return CXPR_IR_RESULT_UNKNOWN;

    case CXPR_NODE_UNARY_OP:
        left_kind = cxpr_ir_infer_fast_result_kind(ast->data.unary_op.operand, reg, depth + 1);
        if (ast->data.unary_op.op == CXPR_TOK_MINUS &&
            (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64)) {
            return left_kind;
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
            if (left_kind == CXPR_IR_RESULT_INT64 && right_kind == CXPR_IR_RESULT_INT64)
                return CXPR_IR_RESULT_INT64;
            if ((left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64) &&
                (right_kind == CXPR_IR_RESULT_DOUBLE || right_kind == CXPR_IR_RESULT_INT64))
                return CXPR_IR_RESULT_DOUBLE;
            return CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_SLASH:
            return ((left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64) &&
                    (right_kind == CXPR_IR_RESULT_DOUBLE || right_kind == CXPR_IR_RESULT_INT64))
                       ? CXPR_IR_RESULT_DOUBLE : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_PERCENT:
        case CXPR_TOK_POWER:
            return ((left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64) &&
                    (right_kind == CXPR_IR_RESULT_DOUBLE || right_kind == CXPR_IR_RESULT_INT64))
                       ? CXPR_IR_RESULT_DOUBLE
                       : CXPR_IR_RESULT_UNKNOWN;
        case CXPR_TOK_LT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GT:
        case CXPR_TOK_GTE:
            return ((left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64) &&
                    (right_kind == CXPR_IR_RESULT_DOUBLE || right_kind == CXPR_IR_RESULT_INT64))
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
            return ((left_kind == right_kind && left_kind == CXPR_IR_RESULT_BOOL) ||
                    ((left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_INT64) &&
                     (right_kind == CXPR_IR_RESULT_DOUBLE || right_kind == CXPR_IR_RESULT_INT64)))
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
            (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_BOOL ||
             left_kind == CXPR_IR_RESULT_INT64)) {
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
                (left_kind == CXPR_IR_RESULT_DOUBLE || left_kind == CXPR_IR_RESULT_BOOL ||
                 left_kind == CXPR_IR_RESULT_INT64)) {
                return left_kind;
            }
            return CXPR_IR_RESULT_UNKNOWN;
        }

        entry = cxpr_registry_find(reg, ast->data.function_call.name);
        if (!entry || entry->ast_func || entry->model_producer ||
            (entry->struct_fields && !entry->struct_producer && !entry->typed_struct_producer) ||
            ((entry->struct_producer || entry->typed_struct_producer) && !entry->sync_func)) {
            return CXPR_IR_RESULT_UNKNOWN;
        }

        if (entry->typed_func && entry->has_return_type) {
            return entry->return_type == CXPR_VALUE_BOOL ? CXPR_IR_RESULT_BOOL :
                   entry->return_type == CXPR_VALUE_NUMBER ? CXPR_IR_RESULT_DOUBLE :
                   entry->return_type == CXPR_VALUE_INT64 ? CXPR_IR_RESULT_INT64 :
                   CXPR_IR_RESULT_UNKNOWN;
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
                   entry->return_type == CXPR_VALUE_INT64 ? CXPR_IR_RESULT_INT64 :
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
