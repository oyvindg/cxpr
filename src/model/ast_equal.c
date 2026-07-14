#include "model/internal.h"

#include <math.h>

static bool cxpr_model_nullable_name_equal(const char* left, const char* right) {
    if (!left || !right) return left == right;
    return cxpr_model_names_match(left, right);
}

bool cxpr_model_ast_equal(const cxpr_ast* left, const cxpr_ast* right) {
    if (left == right) return true;
    if (!left || !right || cxpr_ast_type(left) != cxpr_ast_type(right)) return false;
    switch (cxpr_ast_type(left)) {
    case CXPR_NODE_NUMBER:
        return fabs(cxpr_ast_number_value(left) - cxpr_ast_number_value(right)) < 1e-12;
    case CXPR_NODE_BOOL:
        return cxpr_ast_bool_value(left) == cxpr_ast_bool_value(right);
    case CXPR_NODE_STRING:
        return cxpr_model_nullable_name_equal(cxpr_ast_string_value(left),
                                              cxpr_ast_string_value(right));
    case CXPR_NODE_IDENTIFIER:
        return cxpr_model_names_match(cxpr_ast_identifier_name(left),
                                      cxpr_ast_identifier_name(right));
    case CXPR_NODE_VARIABLE:
        return cxpr_model_names_match(cxpr_ast_variable_name(left),
                                      cxpr_ast_variable_name(right));
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_model_names_match(cxpr_ast_field_object(left),
                                      cxpr_ast_field_object(right)) &&
               cxpr_model_names_match(cxpr_ast_field_name(left),
                                      cxpr_ast_field_name(right));
    case CXPR_NODE_CHAIN_ACCESS: {
        size_t depth = cxpr_ast_chain_depth(left);
        if (depth != cxpr_ast_chain_depth(right)) return false;
        for (size_t i = 0u; i < depth; ++i) {
            if (!cxpr_model_names_match(cxpr_ast_chain_segment(left, i),
                                        cxpr_ast_chain_segment(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_BINARY_OP:
        return cxpr_ast_operator(left) == cxpr_ast_operator(right) &&
               cxpr_model_ast_equal(cxpr_ast_left(left), cxpr_ast_left(right)) &&
               cxpr_model_ast_equal(cxpr_ast_right(left), cxpr_ast_right(right));
    case CXPR_NODE_UNARY_OP:
        return cxpr_ast_operator(left) == cxpr_ast_operator(right) &&
               cxpr_model_ast_equal(cxpr_ast_operand(left), cxpr_ast_operand(right));
    case CXPR_NODE_FUNCTION_CALL: {
        size_t argc = cxpr_ast_function_argc(left);
        if (!cxpr_model_names_match(cxpr_ast_function_name(left),
                                    cxpr_ast_function_name(right)) ||
            argc != cxpr_ast_function_argc(right) ||
            cxpr_ast_function_has_named_args(left) != cxpr_ast_function_has_named_args(right)) {
            return false;
        }
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_nullable_name_equal(cxpr_ast_function_arg_name(left, i),
                                                cxpr_ast_function_arg_name(right, i)) ||
                !cxpr_model_ast_equal(cxpr_ast_function_arg(left, i),
                                      cxpr_ast_function_arg(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_PRODUCER_ACCESS: {
        size_t argc = cxpr_ast_producer_argc(left);
        if (!cxpr_model_names_match(cxpr_ast_producer_name(left),
                                    cxpr_ast_producer_name(right)) ||
            !cxpr_model_names_match(cxpr_ast_producer_field(left),
                                    cxpr_ast_producer_field(right)) ||
            argc != cxpr_ast_producer_argc(right) ||
            cxpr_ast_producer_has_named_args(left) != cxpr_ast_producer_has_named_args(right)) {
            return false;
        }
        for (size_t i = 0u; i < argc; ++i) {
            if (!cxpr_model_nullable_name_equal(cxpr_ast_producer_arg_name(left, i),
                                                cxpr_ast_producer_arg_name(right, i)) ||
                !cxpr_model_ast_equal(cxpr_ast_producer_arg(left, i),
                                      cxpr_ast_producer_arg(right, i))) {
                return false;
            }
        }
        return true;
    }
    case CXPR_NODE_LOOKBACK:
        return cxpr_model_ast_equal(cxpr_ast_lookback_target(left),
                                    cxpr_ast_lookback_target(right)) &&
               cxpr_model_ast_equal(cxpr_ast_lookback_index(left),
                                    cxpr_ast_lookback_index(right));
    case CXPR_NODE_TERNARY:
        return cxpr_model_ast_equal(cxpr_ast_ternary_condition(left),
                                    cxpr_ast_ternary_condition(right)) &&
               cxpr_model_ast_equal(cxpr_ast_ternary_true_branch(left),
                                    cxpr_ast_ternary_true_branch(right)) &&
               cxpr_model_ast_equal(cxpr_ast_ternary_false_branch(left),
                                    cxpr_ast_ternary_false_branch(right));
    case CXPR_NODE_ARRAY:
    default:
        return false;
    }
}
