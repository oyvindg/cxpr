#include "model/internal.h"

#include <math.h>

static bool cxpr_model_nullable_name_equal(const char* left, const char* right) {
    if (!left || !right) return left == right;
    return cxpr_model_names_match(left, right);
}

bool cxpr_model_ast_equal(const cxpr_expr_ast* left, const cxpr_expr_ast* right) {
    if (left == right) return true;
    if (!left || !right || cxpr_expr_ast_kind_of(left) != cxpr_expr_ast_kind_of(right)) return false;
    switch (cxpr_expr_ast_kind_of(left)) {
    case CXPR_NODE_NUMBER:
        return fabs(cxpr_expr_ast_number_value(left) - cxpr_expr_ast_number_value(right)) < 1e-12;
    case CXPR_NODE_BOOL:
        return cxpr_expr_ast_bool_value(left) == cxpr_expr_ast_bool_value(right);
    case CXPR_NODE_STRING:
        return cxpr_model_nullable_name_equal(cxpr_expr_ast_string_value(left),
                                              cxpr_expr_ast_string_value(right));
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_names_match(cxpr_expr_ast_identifier_name(left),
                                      cxpr_expr_ast_identifier_name(right));
    case CXPR_NODE_VARIABLE:
        return cxpr_model_names_match(cxpr_expr_ast_param_name(left),
                                      cxpr_expr_ast_param_name(right));
    case CXPR_NODE_FIELD_ACCESS:
        if (cxpr_expr_ast_field_base(left) || cxpr_expr_ast_field_base(right)) {
            return cxpr_model_ast_equal(cxpr_expr_ast_field_base(left),
                                        cxpr_expr_ast_field_base(right)) &&
                   cxpr_model_names_match(cxpr_expr_ast_field_name(left),
                                          cxpr_expr_ast_field_name(right));
        }
        return cxpr_model_names_match(cxpr_expr_ast_field_object(left),
                                      cxpr_expr_ast_field_object(right)) &&
               cxpr_model_names_match(cxpr_expr_ast_field_name(left),
                                      cxpr_expr_ast_field_name(right));
    case CXPR_NODE_CHAIN_ACCESS: {
        size_t depth = cxpr_expr_ast_chain_count(left);
        if (depth != cxpr_expr_ast_chain_count(right)) return false;
        for (size_t i = 0u; i < depth; ++i) {
            if (!cxpr_model_names_match(cxpr_expr_ast_chain_segment(left, i),
                                        cxpr_expr_ast_chain_segment(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_RECORD: {
        size_t count = cxpr_expr_ast_record_field_count(left);
        if (count != cxpr_expr_ast_record_field_count(right)) return false;
        for (size_t i = 0u; i < count; ++i) {
            if (!cxpr_model_names_match(cxpr_expr_ast_record_field_name(left, i),
                                        cxpr_expr_ast_record_field_name(right, i)) ||
                !cxpr_model_ast_equal(cxpr_expr_ast_record_field_value(left, i),
                                      cxpr_expr_ast_record_field_value(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_expr_ast_operator(left) == cxpr_expr_ast_operator(right) &&
               cxpr_model_ast_equal(cxpr_expr_ast_binary_left(left), cxpr_expr_ast_binary_left(right)) &&
               cxpr_model_ast_equal(cxpr_expr_ast_binary_right(left), cxpr_expr_ast_binary_right(right));
    case CXPR_NODE_UNARY_OP:
        return cxpr_expr_ast_operator(left) == cxpr_expr_ast_operator(right) &&
               cxpr_model_ast_equal(cxpr_expr_ast_unary_operand(left), cxpr_expr_ast_unary_operand(right));
    case CXPR_NODE_FUNCTION_CALL: {
        size_t argc = cxpr_expr_ast_call_arg_count(left);
        if (!cxpr_model_names_match(cxpr_expr_ast_call_name(left),
                                    cxpr_expr_ast_call_name(right)) ||
            argc != cxpr_expr_ast_call_arg_count(right) ||
            cxpr_expr_ast_call_has_named_args(left) != cxpr_expr_ast_call_has_named_args(right)) {
            return false;
        }
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_nullable_name_equal(cxpr_expr_ast_call_arg_name(left, i),
                                                cxpr_expr_ast_call_arg_name(right, i)) ||
                !cxpr_model_ast_equal(cxpr_expr_ast_call_arg(left, i),
                                      cxpr_expr_ast_call_arg(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        size_t argc = cxpr_expr_ast_producer_arg_count(left);
        if (!cxpr_model_names_match(cxpr_expr_ast_producer_name(left),
                                    cxpr_expr_ast_producer_name(right)) ||
            !cxpr_model_names_match(cxpr_expr_ast_producer_field(left),
                                    cxpr_expr_ast_producer_field(right)) ||
            argc != cxpr_expr_ast_producer_arg_count(right) ||
            cxpr_expr_ast_producer_has_named_args(left) != cxpr_expr_ast_producer_has_named_args(right)) {
            return false;
        }
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_nullable_name_equal(cxpr_expr_ast_producer_arg_name(left, i),
                                                cxpr_expr_ast_producer_arg_name(right, i)) ||
                !cxpr_model_ast_equal(cxpr_expr_ast_producer_arg(left, i),
                                      cxpr_expr_ast_producer_arg(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_INDEX:
        return cxpr_model_ast_equal(cxpr_expr_ast_index_target(left),
                                    cxpr_expr_ast_index_target(right)) &&
               cxpr_model_ast_equal(cxpr_expr_ast_index_expression(left),
                                    cxpr_expr_ast_index_expression(right));
    case CXPR_NODE_TERNARY:
        return cxpr_model_ast_equal(cxpr_expr_ast_ternary_condition(left),
                                    cxpr_expr_ast_ternary_condition(right)) &&
               cxpr_model_ast_equal(cxpr_expr_ast_ternary_true(left),
                                    cxpr_expr_ast_ternary_true(right)) &&
               cxpr_model_ast_equal(cxpr_expr_ast_ternary_false(left),
                                    cxpr_expr_ast_ternary_false(right));
    default:
        return false;
    }
}
