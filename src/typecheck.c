/**
 * @file typecheck.c
 * @brief Shared AST typecheck pass for strict boolean positions.
 */

#include "ast/internal.h"
#include "registry/internal.h"
#include <cxpr/typecheck.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum cxpr_typecheck_static_type {
    CXPR_STATIC_UNKNOWN = -2,
    CXPR_STATIC_ERROR = -1,
    CXPR_STATIC_NUMBER = CXPR_VALUE_NUMBER,
    CXPR_STATIC_BOOL = CXPR_VALUE_BOOL,
    CXPR_STATIC_STRUCT = CXPR_VALUE_STRUCT,
    CXPR_STATIC_STRING = CXPR_VALUE_STRING,
    CXPR_STATIC_NULL = CXPR_VALUE_NULL,
    CXPR_STATIC_TIMESTAMP = CXPR_VALUE_TIMESTAMP,
    CXPR_STATIC_DURATION = CXPR_VALUE_DURATION,
    CXPR_STATIC_ARRAY = CXPR_VALUE_ARRAY
} cxpr_typecheck_static_type;

static const char* cxpr_typecheck_type_name(cxpr_typecheck_static_type type) {
    switch (type) {
    case CXPR_STATIC_UNKNOWN: return "unknown";
    case CXPR_STATIC_NUMBER: return "number";
    case CXPR_STATIC_BOOL: return "bool";
    case CXPR_STATIC_STRUCT: return "struct";
    case CXPR_STATIC_STRING: return "string";
    case CXPR_STATIC_NULL: return "null";
    case CXPR_STATIC_TIMESTAMP: return "timestamp";
    case CXPR_STATIC_DURATION: return "duration";
    case CXPR_STATIC_ARRAY: return "array";
    default: return "invalid";
    }
}

static const char* cxpr_typecheck_op_name(int op) {
    switch (op) {
    case CXPR_TOK_PLUS: return "+";
    case CXPR_TOK_MINUS: return "-";
    case CXPR_TOK_STAR: return "*";
    case CXPR_TOK_SLASH: return "/";
    case CXPR_TOK_PERCENT: return "%";
    case CXPR_TOK_POWER: return "^";
    case CXPR_TOK_LT: return "<";
    case CXPR_TOK_LTE: return "<=";
    case CXPR_TOK_GT: return ">";
    case CXPR_TOK_GTE: return ">=";
    case CXPR_TOK_EQ: return "==";
    case CXPR_TOK_NEQ: return "!=";
    case CXPR_TOK_AND: return "and";
    case CXPR_TOK_OR: return "or";
    case CXPR_TOK_NOT: return "not";
    default: return "operator";
    }
}

static bool cxpr_typecheck_error(cxpr_error* err,
                            const char* op,
                            const char* expected,
                            const char* role,
                            const cxpr_expr_ast* node,
                            cxpr_typecheck_static_type actual) {
    static _Thread_local char message[512];
    char* expr = cxpr_expr_ast_to_string(node);

    if (err) {
        snprintf(message, sizeof(message),
                 "type error: '%s' requires %s, but %s '%s' has type %s",
                 op ? op : "expression",
                 expected ? expected : "compatible type",
                 role ? role : "expression",
                 expr ? expr : "<expr>",
                 cxpr_typecheck_type_name(actual));
        *err = (cxpr_error){0};
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = message;
    }
    free(expr);
    return false;
}

static bool cxpr_typecheck_struct_shape_error(cxpr_error* err, const char* op) {
    static _Thread_local char message[256];

    if (err) {
        snprintf(message, sizeof(message),
                 "type error: '%s' requires matching struct fields",
                 op ? op : "operator");
        *err = (cxpr_error){0};
        err->code = CXPR_ERR_TYPE_MISMATCH;
        err->message = message;
    }
    return false;
}

static bool cxpr_typecheck_record_has_field(const cxpr_expr_ast* record,
                                            const char* field_name) {
    if (!record || record->type != CXPR_NODE_RECORD || !field_name) return false;
    for (size_t i = 0u; i < record->data.record.field_count; ++i) {
        if (strcmp(record->data.record.field_names[i], field_name) == 0) {
            return true;
        }
    }
    return false;
}

static bool cxpr_typecheck_record_shapes_match(const cxpr_expr_ast* left,
                                               const cxpr_expr_ast* right) {
    if (!left || !right ||
        left->type != CXPR_NODE_RECORD ||
        right->type != CXPR_NODE_RECORD) {
        return true;
    }
    if (left->data.record.field_count != right->data.record.field_count) return false;
    for (size_t i = 0u; i < left->data.record.field_count; ++i) {
        if (!cxpr_typecheck_record_has_field(right, left->data.record.field_names[i])) {
            return false;
        }
    }
    return true;
}

static bool cxpr_typecheck_is_numeric(cxpr_typecheck_static_type type) {
    return type == CXPR_STATIC_NUMBER || type == CXPR_STATIC_UNKNOWN;
}

static bool cxpr_typecheck_is_struct_arithmetic_operand(cxpr_typecheck_static_type type) {
    return type == CXPR_STATIC_NUMBER ||
           type == CXPR_STATIC_STRUCT ||
           type == CXPR_STATIC_UNKNOWN;
}

static bool cxpr_typecheck_is_bool(cxpr_typecheck_static_type type) {
    return type == CXPR_STATIC_BOOL || type == CXPR_STATIC_UNKNOWN;
}

static cxpr_typecheck_static_type cxpr_typecheck_join(cxpr_typecheck_static_type a,
                                       cxpr_typecheck_static_type b,
                                       const cxpr_expr_ast* node,
                                       cxpr_error* err) {
    if (a == CXPR_STATIC_ERROR || b == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
    if (a == b) return a;
    if (a == CXPR_STATIC_UNKNOWN) return b;
    if (b == CXPR_STATIC_UNKNOWN) return a;
    (void)cxpr_typecheck_error(err, "?:", "branches with matching types",
                          "ternary branch", node, b);
    return CXPR_STATIC_ERROR;
}

static cxpr_typecheck_static_type cxpr_typecheck_infer(const cxpr_expr_ast* ast,
                                        const cxpr_registry* reg,
                                        cxpr_error* err);

static cxpr_typecheck_static_type cxpr_typecheck_infer_binary(const cxpr_expr_ast* ast,
                                               const cxpr_registry* reg,
                                               cxpr_error* err) {
    int op = ast->data.binary_op.op;
    const cxpr_expr_ast* left = ast->data.binary_op.left;
    const cxpr_expr_ast* right = ast->data.binary_op.right;
    cxpr_typecheck_static_type lt = cxpr_typecheck_infer(left, reg, err);
    cxpr_typecheck_static_type rt;

    if (lt == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
    rt = cxpr_typecheck_infer(right, reg, err);
    if (rt == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;

    switch (op) {
    case CXPR_TOK_PLUS:
    case CXPR_TOK_MINUS:
    case CXPR_TOK_STAR:
    case CXPR_TOK_SLASH:
        if (!cxpr_typecheck_is_struct_arithmetic_operand(lt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op),
                                 "number or struct operands", "left operand", left, lt);
            return CXPR_STATIC_ERROR;
        }
        if (!cxpr_typecheck_is_struct_arithmetic_operand(rt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op),
                                 "number or struct operands", "right operand", right, rt);
            return CXPR_STATIC_ERROR;
        }
        if (!cxpr_typecheck_record_shapes_match(left, right)) {
            cxpr_typecheck_struct_shape_error(err, cxpr_typecheck_op_name(op));
            return CXPR_STATIC_ERROR;
        }
        return (lt == CXPR_STATIC_STRUCT || rt == CXPR_STATIC_STRUCT)
                   ? CXPR_STATIC_STRUCT
                   : CXPR_STATIC_NUMBER;
    case CXPR_TOK_PERCENT:
    case CXPR_TOK_POWER:
        if (!cxpr_typecheck_is_numeric(lt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "number operands", "left operand", left, lt);
            return CXPR_STATIC_ERROR;
        }
        if (!cxpr_typecheck_is_numeric(rt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "number operands", "right operand", right, rt);
            return CXPR_STATIC_ERROR;
        }
        return CXPR_STATIC_NUMBER;
    case CXPR_TOK_LT:
    case CXPR_TOK_LTE:
    case CXPR_TOK_GT:
    case CXPR_TOK_GTE:
        if (!cxpr_typecheck_is_numeric(lt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "number operands", "left operand", left, lt);
            return CXPR_STATIC_ERROR;
        }
        if (!cxpr_typecheck_is_numeric(rt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "number operands", "right operand", right, rt);
            return CXPR_STATIC_ERROR;
        }
        return CXPR_STATIC_BOOL;
    case CXPR_TOK_EQ:
    case CXPR_TOK_NEQ:
        if (lt != CXPR_STATIC_UNKNOWN && rt != CXPR_STATIC_UNKNOWN && lt != rt) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "matching operand types",
                            "right operand", right, rt);
            return CXPR_STATIC_ERROR;
        }
        return CXPR_STATIC_BOOL;
    case CXPR_TOK_AND:
    case CXPR_TOK_OR:
        if (!cxpr_typecheck_is_bool(lt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "bool operands", "left operand", left, lt);
            return CXPR_STATIC_ERROR;
        }
        if (!cxpr_typecheck_is_bool(rt)) {
            cxpr_typecheck_error(err, cxpr_typecheck_op_name(op), "bool operands", "right operand", right, rt);
            return CXPR_STATIC_ERROR;
        }
        return CXPR_STATIC_BOOL;
    default:
        return CXPR_STATIC_UNKNOWN;
    }
}

static cxpr_typecheck_static_type cxpr_typecheck_infer_call(const cxpr_expr_ast* ast,
                                             const cxpr_registry* reg,
                                             cxpr_error* err) {
    cxpr_func_entry* entry;
    const char* name = ast->data.function_call.name;

    if (strcmp(name, "if") == 0 && ast->data.function_call.argc == 3u) {
        cxpr_typecheck_static_type cond_type =
            cxpr_typecheck_infer(ast->data.function_call.args[0], reg, err);
        cxpr_typecheck_static_type true_type;
        cxpr_typecheck_static_type false_type;
        if (cond_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        if (!cxpr_typecheck_is_bool(cond_type) && !cxpr_typecheck_is_numeric(cond_type)) {
            cxpr_typecheck_error(err, name, "bool or number condition", "condition",
                                 ast->data.function_call.args[0], cond_type);
            return CXPR_STATIC_ERROR;
        }
        true_type = cxpr_typecheck_infer(ast->data.function_call.args[1], reg, err);
        if (true_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        false_type = cxpr_typecheck_infer(ast->data.function_call.args[2], reg, err);
        if (false_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        return cxpr_typecheck_join(true_type, false_type,
                                   ast->data.function_call.args[2], err);
    }

    for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
        cxpr_typecheck_static_type arg_type = cxpr_typecheck_infer(ast->data.function_call.args[i], reg, err);
        if (arg_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        if ((strcmp(name, "any") == 0 || strcmp(name, "all") == 0) &&
            !cxpr_typecheck_is_bool(arg_type)) {
            cxpr_typecheck_error(err, name, "bool argument", "argument",
                            ast->data.function_call.args[i], arg_type);
            return CXPR_STATIC_ERROR;
        }
    }

    entry = cxpr_registry_find(reg, name);
    if (entry && entry->has_return_type) return (cxpr_typecheck_static_type)entry->return_type;
    return CXPR_STATIC_UNKNOWN;
}

static cxpr_typecheck_static_type cxpr_typecheck_infer(const cxpr_expr_ast* ast,
                                        const cxpr_registry* reg,
                                        cxpr_error* err) {
    cxpr_typecheck_static_type operand_type;
    cxpr_typecheck_static_type true_type;
    cxpr_typecheck_static_type false_type;

    if (!ast) return CXPR_STATIC_UNKNOWN;

    switch (ast->type) {
    case CXPR_NODE_NUMBER: return CXPR_STATIC_NUMBER;
    case CXPR_NODE_BOOL: return CXPR_STATIC_BOOL;
    case CXPR_NODE_STRING: return CXPR_STATIC_STRING;
    case CXPR_NODE_ARRAY: return CXPR_STATIC_ARRAY;
    case CXPR_NODE_RECORD:
        for (size_t i = 0u; i < ast->data.record.field_count; ++i) {
            cxpr_typecheck_static_type field_type =
                cxpr_typecheck_infer(ast->data.record.field_values[i], reg, err);
            if (field_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        }
        return CXPR_STATIC_STRUCT;
    case CXPR_NODE_IDENTIFIER:
    case CXPR_NODE_VARIABLE:
    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
    case CXPR_NODE_PRODUCER_ACCESS:
        return CXPR_STATIC_UNKNOWN;
    case CXPR_NODE_BINARY_OP:
        return cxpr_typecheck_infer_binary(ast, reg, err);
    case CXPR_NODE_UNARY_OP:
        operand_type = cxpr_typecheck_infer(ast->data.unary_op.operand, reg, err);
        if (operand_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        if (ast->data.unary_op.op == CXPR_TOK_MINUS) {
            if (!cxpr_typecheck_is_numeric(operand_type)) {
                cxpr_typecheck_error(err, "-", "number operand", "operand",
                                ast->data.unary_op.operand, operand_type);
                return CXPR_STATIC_ERROR;
            }
            return CXPR_STATIC_NUMBER;
        }
        if (ast->data.unary_op.op == CXPR_TOK_NOT) {
            if (!cxpr_typecheck_is_bool(operand_type)) {
                cxpr_typecheck_error(err, "not", "bool operand", "operand",
                                ast->data.unary_op.operand, operand_type);
                return CXPR_STATIC_ERROR;
            }
            return CXPR_STATIC_BOOL;
        }
        return CXPR_STATIC_UNKNOWN;
    case CXPR_NODE_FUNCTION_CALL:
        return cxpr_typecheck_infer_call(ast, reg, err);
    case CXPR_NODE_LOOKBACK:
        return cxpr_typecheck_infer(ast->data.lookback.target, reg, err);
    case CXPR_NODE_TERNARY:
        operand_type = cxpr_typecheck_infer(ast->data.ternary.condition, reg, err);
        if (operand_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        if (!cxpr_typecheck_is_bool(operand_type)) {
            cxpr_typecheck_error(err, "?:", "bool condition", "condition",
                            ast->data.ternary.condition, operand_type);
            return CXPR_STATIC_ERROR;
        }
        true_type = cxpr_typecheck_infer(ast->data.ternary.true_branch, reg, err);
        if (true_type == CXPR_STATIC_ERROR) return CXPR_STATIC_ERROR;
        false_type = cxpr_typecheck_infer(ast->data.ternary.false_branch, reg, err);
        return cxpr_typecheck_join(true_type, false_type, ast->data.ternary.false_branch, err);
    }

    return CXPR_STATIC_UNKNOWN;
}

bool cxpr_typecheck(const cxpr_expr_ast* ast, const cxpr_registry* reg,
                    cxpr_value_type* out_type, cxpr_error* err) {
    cxpr_typecheck_static_type type;

    if (err) *err = (cxpr_error){0};
    if (!ast) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL AST";
        }
        return false;
    }

    type = cxpr_typecheck_infer(ast, reg, err);
    if (type == CXPR_STATIC_ERROR) return false;
    if (out_type && type != CXPR_STATIC_UNKNOWN) *out_type = (cxpr_value_type)type;
    return true;
}

bool cxpr_typecheck_bool_root(const cxpr_expr_ast* ast, const cxpr_registry* reg,
                              cxpr_error* err) {
    cxpr_typecheck_static_type type;

    if (err) *err = (cxpr_error){0};
    if (!ast) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "NULL AST";
        }
        return false;
    }

    type = cxpr_typecheck_infer(ast, reg, err);
    if (type == CXPR_STATIC_ERROR) return false;
    if (!cxpr_typecheck_is_bool(type)) {
        return cxpr_typecheck_error(err, "bool root", "bool expression", "root", ast, type);
    }
    return true;
}
