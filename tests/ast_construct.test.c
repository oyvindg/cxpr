#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_public_constructors_cover_split_ast_nodes(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast** args = (cxpr_ast**)calloc(2, sizeof(*args));
    cxpr_ast* fn;
    cxpr_ast* producer;
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
    cxpr_ast_free(producer);
    cxpr_ast_free(chain);
    cxpr_ast_free(fn);
    cxpr_parser_free(parser);
}

int main(void) {
    test_public_constructors_cover_split_ast_nodes();
    printf("  \xE2\x9C\x93 ast_construct\n");
    return 0;
}
