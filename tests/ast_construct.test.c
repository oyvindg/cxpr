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
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast** args = (cxpr_ast**)calloc(2, sizeof(*args));
    cxpr_ast* fn;
    cxpr_ast* producer;
    cxpr_ast* producer_named;
    cxpr_ast* variable;
    cxpr_ast* chain;
    cxpr_ast* lookback;
    cxpr_ast* ternary;

    assert(parser);
    assert(args);
    args[0] = cxpr_ast_new_number(12.0);
    args[1] = cxpr_ast_new_number(26.0);
    fn = cxpr_ast_new_function_call("ema_pair", args, 2);
    assert(fn);
    assert(cxpr_ast_type(fn) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(fn), "ema_pair") == 0);
    assert(cxpr_ast_function_argc(fn) == 2);
    assert(cxpr_ast_function_arg_name(fn, 0) == NULL);
    assert(!cxpr_ast_function_has_named_args(fn));

    {
        cxpr_ast** named_args = (cxpr_ast**)calloc(2, sizeof(*named_args));
        char** arg_names = (char**)calloc(2, sizeof(*arg_names));
        cxpr_ast* named_fn;
        assert(named_args);
        assert(arg_names);
        named_args[0] = cxpr_ast_new_number(9.0);
        named_args[1] = cxpr_ast_new_number(21.0);
        arg_names[0] = test_strdup("fast");
        arg_names[1] = test_strdup("slow");
        named_fn = cxpr_ast_new_function_call_named("macd", named_args, arg_names, 2);
        assert(named_fn);
        assert(cxpr_ast_function_has_named_args(named_fn));
        assert(strcmp(cxpr_ast_function_arg_name(named_fn, 0), "fast") == 0);
        assert(strcmp(cxpr_ast_function_arg_name(named_fn, 1), "slow") == 0);
        cxpr_ast_free(named_fn);
    }

    variable = cxpr_ast_new_variable("period");
    assert(variable);
    assert(cxpr_ast_type(variable) == CXPR_NODE_VARIABLE);
    assert(strcmp(cxpr_ast_variable_name(variable), "period") == 0);

    chain = cxpr_parse(parser, "body.velocity.x", &err);
    assert(chain);
    assert(cxpr_ast_type(chain) == CXPR_NODE_CHAIN_ACCESS);
    assert(cxpr_ast_chain_depth(chain) == 3);
    assert(strcmp(cxpr_ast_chain_segment(chain, 2), "x") == 0);

    producer = cxpr_parse(parser, "macd(fast=9, slow=21).signal", &err);
    assert(producer);
    assert(cxpr_ast_type(producer) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_ast_producer_name(producer), "macd") == 0);
    assert(strcmp(cxpr_ast_producer_field(producer), "signal") == 0);
    assert(cxpr_ast_producer_has_named_args(producer));

    {
        cxpr_ast** producer_args = (cxpr_ast**)calloc(1, sizeof(*producer_args));
        char** producer_arg_names = (char**)calloc(1, sizeof(*producer_arg_names));
        assert(producer_args);
        assert(producer_arg_names);
        producer_args[0] = cxpr_ast_new_number(14.0);
        producer_arg_names[0] = test_strdup("period");
        producer_named = cxpr_ast_new_producer_access_named(
            "rsi",
            producer_args,
            producer_arg_names,
            1,
            "value");
        assert(producer_named);
        assert(cxpr_ast_type(producer_named) == CXPR_NODE_PRODUCER_ACCESS);
        assert(cxpr_ast_producer_has_named_args(producer_named));
        assert(strcmp(cxpr_ast_producer_arg_name(producer_named, 0), "period") == 0);
        assert(strcmp(cxpr_ast_producer_field(producer_named), "value") == 0);
    }

    {
        cxpr_ast** producer_args = (cxpr_ast**)calloc(1, sizeof(*producer_args));
        cxpr_ast* producer_plain;
        assert(producer_args);
        producer_args[0] = cxpr_ast_new_number(20.0);
        producer_plain = cxpr_ast_new_producer_access("bb", producer_args, 1, "upper");
        assert(producer_plain);
        assert(cxpr_ast_type(producer_plain) == CXPR_NODE_PRODUCER_ACCESS);
        assert(!cxpr_ast_producer_has_named_args(producer_plain));
        assert(strcmp(cxpr_ast_producer_field(producer_plain), "upper") == 0);
        cxpr_ast_free(producer_plain);
    }

    lookback = cxpr_ast_new_lookback(cxpr_ast_new_identifier("close"), cxpr_ast_new_number(1.0));
    assert(lookback);
    assert(cxpr_ast_type(lookback) == CXPR_NODE_LOOKBACK);
    assert(cxpr_ast_type(cxpr_ast_lookback_target(lookback)) == CXPR_NODE_IDENTIFIER);

    ternary = cxpr_ast_new_ternary(cxpr_ast_new_bool(true),
                                   cxpr_ast_new_number(1.0),
                                   cxpr_ast_new_number(0.0));
    assert(ternary);
    assert(cxpr_ast_type(ternary) == CXPR_NODE_TERNARY);
    assert(cxpr_ast_type(cxpr_ast_ternary_condition(ternary)) == CXPR_NODE_BOOL);

    cxpr_ast_free(ternary);
    cxpr_ast_free(lookback);
    cxpr_ast_free(producer_named);
    cxpr_ast_free(producer);
    cxpr_ast_free(chain);
    cxpr_ast_free(variable);
    cxpr_ast_free(fn);
    cxpr_parser_free(parser);
}

int main(void) {
    test_public_constructors_cover_split_ast_nodes();
    printf("  \xE2\x9C\x93 ast_construct\n");
    return 0;
}
