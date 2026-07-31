#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* test_strdup(const char* text) {
    size_t len = strlen(text);
    char* out = (char*)malloc(len + 1u);
    assert(out);
    memcpy(out, text, len + 1u);
    return out;
}

static void test_public_constructors_cover_split_ast_nodes(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast** args = (cxpr_expr_ast**)calloc(2, sizeof(*args));
    cxpr_expr_ast* fn;
    cxpr_expr_ast* producer;
    cxpr_expr_ast* producer_named;
    cxpr_expr_ast* variable;
    cxpr_expr_ast* chain;
    cxpr_expr_ast* index;
    cxpr_expr_ast* lookback;
    cxpr_expr_ast* ternary;

    assert(parser);
    assert(args);
    args[0] = cxpr_expr_ast_number_new(12.0);
    args[1] = cxpr_expr_ast_number_new(26.0);
    fn = cxpr_expr_ast_call_new("ema_pair", args, 2);
    assert(fn);
    assert(cxpr_expr_ast_kind_of(fn) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_expr_ast_call_name(fn), "ema_pair") == 0);
    assert(cxpr_expr_ast_call_arg_count(fn) == 2);
    assert(cxpr_expr_ast_call_arg_name(fn, 0) == NULL);
    assert(!cxpr_expr_ast_call_has_named_args(fn));

    {
        cxpr_expr_ast** named_args = (cxpr_expr_ast**)calloc(2, sizeof(*named_args));
        char** arg_names = (char**)calloc(2, sizeof(*arg_names));
        cxpr_expr_ast* named_fn;
        assert(named_args);
        assert(arg_names);
        named_args[0] = cxpr_expr_ast_number_new(9.0);
        named_args[1] = cxpr_expr_ast_number_new(21.0);
        arg_names[0] = test_strdup("fast");
        arg_names[1] = test_strdup("slow");
        named_fn = cxpr_expr_ast_call_named_new("macd", named_args, arg_names, 2);
        assert(named_fn);
        assert(cxpr_expr_ast_call_has_named_args(named_fn));
        assert(strcmp(cxpr_expr_ast_call_arg_name(named_fn, 0), "fast") == 0);
        assert(strcmp(cxpr_expr_ast_call_arg_name(named_fn, 1), "slow") == 0);
        cxpr_expr_ast_free(named_fn);
    }

    variable = cxpr_expr_ast_param_new("period");
    assert(variable);
    assert(cxpr_expr_ast_kind_of(variable) == CXPR_NODE_VARIABLE);
    assert(strcmp(cxpr_expr_ast_param_name(variable), "period") == 0);

    chain = cxpr_expr_ast_parse(parser, "body.velocity.x", &err);
    assert(chain);
    assert(cxpr_expr_ast_kind_of(chain) == CXPR_NODE_CHAIN_ACCESS);
    assert(cxpr_expr_ast_chain_count(chain) == 3);
    assert(strcmp(cxpr_expr_ast_chain_segment(chain, 2), "x") == 0);

    producer = cxpr_expr_ast_parse(parser, "macd(fast=9, slow=21).signal", &err);
    assert(producer);
    assert(cxpr_expr_ast_kind_of(producer) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_expr_ast_producer_name(producer), "macd") == 0);
    assert(strcmp(cxpr_expr_ast_producer_field(producer), "signal") == 0);
    assert(cxpr_expr_ast_producer_has_named_args(producer));

    {
        cxpr_expr_ast** producer_args = (cxpr_expr_ast**)calloc(1, sizeof(*producer_args));
        char** producer_arg_names = (char**)calloc(1, sizeof(*producer_arg_names));
        assert(producer_args);
        assert(producer_arg_names);
        producer_args[0] = cxpr_expr_ast_number_new(14.0);
        producer_arg_names[0] = test_strdup("period");
        producer_named = cxpr_expr_ast_producer_field_named_new(
            "rsi",
            producer_args,
            producer_arg_names,
            1,
            "value");
        assert(producer_named);
        assert(cxpr_expr_ast_kind_of(producer_named) == CXPR_NODE_PRODUCER_ACCESS);
        assert(cxpr_expr_ast_producer_has_named_args(producer_named));
        assert(strcmp(cxpr_expr_ast_producer_arg_name(producer_named, 0), "period") == 0);
        assert(strcmp(cxpr_expr_ast_producer_field(producer_named), "value") == 0);
    }

    {
        cxpr_expr_ast** producer_args = (cxpr_expr_ast**)calloc(1, sizeof(*producer_args));
        cxpr_expr_ast* producer_plain;
        assert(producer_args);
        producer_args[0] = cxpr_expr_ast_number_new(20.0);
        producer_plain = cxpr_expr_ast_producer_field_new("bb", producer_args, 1, "upper");
        assert(producer_plain);
        assert(cxpr_expr_ast_kind_of(producer_plain) == CXPR_NODE_PRODUCER_ACCESS);
        assert(!cxpr_expr_ast_producer_has_named_args(producer_plain));
        assert(strcmp(cxpr_expr_ast_producer_field(producer_plain), "upper") == 0);
        cxpr_expr_ast_free(producer_plain);
    }

    index = cxpr_expr_ast_index_new(
        cxpr_expr_ast_identifier_new("values"), cxpr_expr_ast_number_new(2.0));
    assert(index);
    assert(cxpr_expr_ast_kind_of(index) == CXPR_NODE_INDEX);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_index_target(index)) == CXPR_NODE_IDENTIFIER);
    assert(cxpr_expr_ast_number_value(cxpr_expr_ast_index_expression(index)) == 2.0);
    assert(CXPR_NODE_LOOKBACK == CXPR_NODE_INDEX);

    lookback = cxpr_expr_ast_lookback_new(
        cxpr_expr_ast_identifier_new("close"), cxpr_expr_ast_number_new(1.0));
    assert(lookback);
    assert(cxpr_expr_ast_kind_of(lookback) == CXPR_NODE_LOOKBACK);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_lookback_target(lookback)) == CXPR_NODE_IDENTIFIER);

    ternary = cxpr_expr_ast_ternary_new(cxpr_expr_ast_bool_new(true),
                                   cxpr_expr_ast_number_new(1.0),
                                   cxpr_expr_ast_number_new(0.0));
    assert(ternary);
    assert(cxpr_expr_ast_kind_of(ternary) == CXPR_NODE_TERNARY);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_ternary_condition(ternary)) == CXPR_NODE_BOOL);

    cxpr_expr_ast_free(ternary);
    cxpr_expr_ast_free(lookback);
    cxpr_expr_ast_free(index);
    cxpr_expr_ast_free(producer_named);
    cxpr_expr_ast_free(producer);
    cxpr_expr_ast_free(chain);
    cxpr_expr_ast_free(variable);
    cxpr_expr_ast_free(fn);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_public_constructors_cover_split_ast_nodes();
    printf("  \xE2\x9C\x93 ast_construct\n");
    return 0;
}
