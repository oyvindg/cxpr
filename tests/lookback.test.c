#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool test_direct_lookback_resolver(const cxpr_ast* target,
                                          const cxpr_ast* index_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_value* out_value,
                                          cxpr_error* err) {
    double index = 0.0;
    (void)target;
    (void)userdata;
    if (!cxpr_eval_ast_number(index_ast, ctx, reg, &index, err)) return true;
    *out_value = cxpr_fv_double(100.0 + index);
    return true;
}

static void test_series_lookback_parses_as_native_node(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "close[1]", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_ast_identifier_name(cxpr_ast_lookback_target(ast)), "close") == 0);
    assert(cxpr_ast_type(cxpr_ast_lookback_index(ast)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(ast)) == 1.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_series_lookback_parses_as_native_node\n");
}

static void test_indicator_field_lookback_parses_as_native_node(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "macd(12, 26, 9).signal[2]", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_ast_producer_name(cxpr_ast_lookback_target(ast)), "macd") == 0);
    assert(strcmp(cxpr_ast_producer_field(cxpr_ast_lookback_target(ast)), "signal") == 0);
    assert(cxpr_ast_type(cxpr_ast_lookback_index(ast)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(ast)) == 2.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_indicator_field_lookback_parses_as_native_node\n");
}

static void test_named_arg_indicator_field_lookback_parses_as_native_node(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast =
        cxpr_parse(parser, "macd(fast=$f, slow=$s, signal=$sig).signal[2]", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_ast_producer_name(cxpr_ast_lookback_target(ast)), "macd") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(cxpr_ast_lookback_target(ast), 0), "fast") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(cxpr_ast_lookback_target(ast), 1), "slow") == 0);
    assert(strcmp(cxpr_ast_producer_arg_name(cxpr_ast_lookback_target(ast), 2), "signal") == 0);
    assert(strcmp(cxpr_ast_producer_field(cxpr_ast_lookback_target(ast)), "signal") == 0);
    assert(cxpr_ast_type(cxpr_ast_lookback_index(ast)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(ast)) == 2.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_named_arg_indicator_field_lookback_parses_as_native_node\n");
}

static void test_aggregate_lookback_wraps_aggregate_call(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "avg(z_score($primary, $pair, 60))[1]", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(cxpr_ast_lookback_target(ast)), "avg") == 0);
    assert(cxpr_ast_function_argc(cxpr_ast_lookback_target(ast)) == 1);
    assert(cxpr_ast_type(cxpr_ast_lookback_index(ast)) == CXPR_NODE_NUMBER);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(ast)) == 1.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_aggregate_lookback_wraps_aggregate_call\n");
}

static void test_nested_lookback_stacks_as_ast_nodes(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "close[1][2]", &err);
    assert(ast != NULL && err.code == CXPR_OK);
    assert(cxpr_ast_type(ast) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(ast)) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(ast)) == 2.0);
    assert(cxpr_ast_number_value(cxpr_ast_lookback_index(cxpr_ast_lookback_target(ast))) == 1.0);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_nested_lookback_stacks_as_ast_nodes\n");
}

static void test_eval_ast_at_lookback_and_offset_call_public_wrappers(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* target;
    cxpr_ast* index;
    cxpr_value value = {0};

    assert(parser != NULL);
    assert(ctx != NULL);
    assert(reg != NULL);
    cxpr_registry_set_lookback_resolver(reg, test_direct_lookback_resolver, NULL, NULL);

    target = cxpr_parse(parser, "close", &err);
    assert(target != NULL);
    index = cxpr_ast_new_number(3.0);
    assert(index != NULL);

    assert(cxpr_eval_ast_at_lookback(target, index, ctx, reg, &value, &err));
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 103.0);

    value = (cxpr_value){0};
    assert(cxpr_eval_ast_at_offset(target, 5.0, ctx, reg, &value, &err));
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 105.0);

    cxpr_ast_free(index);
    cxpr_ast_free(target);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    printf("  ✓ test_eval_ast_at_lookback_and_offset_call_public_wrappers\n");
}

int main(void) {
    printf("Running lookback tests...\n");
    test_series_lookback_parses_as_native_node();
    test_indicator_field_lookback_parses_as_native_node();
    test_named_arg_indicator_field_lookback_parses_as_native_node();
    test_aggregate_lookback_wraps_aggregate_call();
    test_nested_lookback_stacks_as_ast_nodes();
    test_eval_ast_at_lookback_and_offset_call_public_wrappers();
    printf("All lookback tests passed!\n");
    return 0;
}
