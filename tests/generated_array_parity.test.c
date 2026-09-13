#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "generated_array_functions.h"

static double evaluate(const char* source, cxpr_context* context,
                       cxpr_registry* registry, int compiled) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &err);
    cxpr_value value = {0};
    assert(ast && err.code == CXPR_OK);
    if (compiled) {
        cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &err);
        assert(program);
        assert(cxpr_expr_compiled_eval(program, context, registry, &value, &err));
        cxpr_expr_compiled_free(program);
    } else {
        assert(cxpr_eval_ast(ast, context, registry, &value, &err));
    }
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER || value.type == CXPR_VALUE_BOOL);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return value.type == CXPR_VALUE_BOOL ? (value.b ? 1.0 : 0.0) : value.d;
}

static void assert_parity(const char* source, cxpr_context* context,
                          cxpr_registry* registry, double generated) {
    assert(evaluate(source, context, registry, 0) == generated);
    assert(evaluate(source, context, registry, 1) == generated);
}

static void assert_invalid(const char* source, cxpr_context* context,
                           cxpr_registry* registry, double generated) {
    for (int compiled = 0; compiled < 2; ++compiled) {
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_error err = {0};
        cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &err);
        cxpr_value value = {0};
        assert(ast);
        if (compiled) {
            cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &err);
            assert(program);
            assert(!cxpr_expr_compiled_eval(program, context, registry, &value, &err));
            cxpr_expr_compiled_free(program);
        } else {
            assert(!cxpr_eval_ast(ast, context, registry, &value, &err));
        }
        assert(err.code == CXPR_ERR_INVALID_INDEX ||
               err.code == CXPR_ERR_INDEX_OUT_OF_RANGE);
        cxpr_expr_ast_free(ast);
        cxpr_expr_parser_free(parser);
    }
    assert(isnan(generated));
}

static void assert_model_status(double index, unsigned status, double expected) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(
        "model array_status\n"
        "$index = 0\n"
        "value = [10, 20, 30][$index]\n"
        "out value\n", &err);
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    double value = 0.0;
    assert(model);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session);
    context = cxpr_model_session_context(session);
    cxpr_context_set_param(context, "index", index);
    if (status == CXPR_C_EVAL_OK) {
        assert(cxpr_model_session_tick(program, session, NULL, &err));
        assert(cxpr_model_session_get_number(session, "value", &value));
        assert(value == expected);
    } else {
        assert(!cxpr_model_session_tick(program, session, NULL, &err));
        assert((status == CXPR_C_EVAL_INVALID_INDEX &&
                err.code == CXPR_ERR_INVALID_INDEX) ||
               (status == CXPR_C_EVAL_INDEX_OUT_OF_RANGE &&
                err.code == CXPR_ERR_INDEX_OUT_OF_RANGE));
    }
    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

int main(void) {
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    assert(context && registry);
    cxpr_register_defaults(registry);
    for (size_t index = 0u; index < 3u; ++index) {
        const cxpr_c_checked_result checked =
            generated_number_index_checked((double)index);
        cxpr_context_set_param(context, "index", (double)index);
        assert_parity("[10, 20, 30][$index]", context, registry,
                      generated_number_index((double)index));
        assert_parity("[true, false, true][$index]", context, registry,
                      generated_bool_index((double)index));
        assert(checked.status == CXPR_C_EVAL_OK);
        assert(checked.value == generated_number_index((double)index));
        assert_model_status((double)index, checked.status, checked.value);
    }
    for (size_t outer = 0u; outer < 2u; ++outer) {
        for (size_t inner = 0u; inner < 2u; ++inner) {
            cxpr_context_set_param(context, "outer", (double)outer);
            cxpr_context_set_param(context, "inner", (double)inner);
            assert_parity("[[1, 2], [3, 4]][$outer][$inner]", context, registry,
                          generated_nested_index((double)outer, (double)inner));
        }
    }
    cxpr_context_set_param(context, "index", -1.0);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(-1.0));
    assert(generated_number_index_checked(-1.0).status == CXPR_C_EVAL_INVALID_INDEX);
    assert_model_status(-1.0, CXPR_C_EVAL_INVALID_INDEX, NAN);
    cxpr_context_set_param(context, "index", 0.5);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(0.5));
    assert(generated_number_index_checked(0.5).status == CXPR_C_EVAL_INVALID_INDEX);
    assert_model_status(0.5, CXPR_C_EVAL_INVALID_INDEX, NAN);
    cxpr_context_set_param(context, "index", NAN);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(NAN));
    assert(generated_number_index_checked(NAN).status == CXPR_C_EVAL_INVALID_INDEX);
    assert_model_status(NAN, CXPR_C_EVAL_INVALID_INDEX, NAN);
    cxpr_context_set_param(context, "index", INFINITY);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(INFINITY));
    assert(generated_number_index_checked(INFINITY).status == CXPR_C_EVAL_INVALID_INDEX);
    assert_model_status(INFINITY, CXPR_C_EVAL_INVALID_INDEX, NAN);
    cxpr_context_set_param(context, "index", 1e30);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(1e30));
    assert(generated_number_index_checked(1e30).status == CXPR_C_EVAL_INVALID_INDEX);
    assert_model_status(1e30, CXPR_C_EVAL_INVALID_INDEX, NAN);
    cxpr_context_set_param(context, "index", 3.0);
    assert_invalid("[10, 20, 30][$index]", context, registry,
                   generated_number_index(3.0));
    assert(generated_number_index_checked(3.0).status == CXPR_C_EVAL_INDEX_OUT_OF_RANGE);
    assert_model_status(3.0, CXPR_C_EVAL_INDEX_OUT_OF_RANGE, NAN);
    assert(isnan(generated_aggregate_index(0.0)));
    assert(generated_aggregate_index_checked(0.0).status ==
           CXPR_C_EVAL_UNSUPPORTED_RESULT);
    cxpr_registry_free(registry);
    cxpr_context_free(context);
    puts("generated array tree/IR/C parity OK");
    return 0;
}
