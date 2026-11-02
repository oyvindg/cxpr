#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_explicit_instance_identity(void) {
    cxpr_error err = {0};
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "voice@left(pitch=2).sample", &err);
    cxpr_expr_ast* clone;
    char* printed;
    char* key;
    assert(ast && err.code == CXPR_OK);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_expr_ast_call_instance_id(ast), "left") == 0);
    printed = cxpr_expr_ast_to_string(ast);
    assert(printed && strcmp(printed, "voice@left(pitch=2).sample") == 0);
    key = cxpr_expr_ast_call_identity_key(ast);
    assert(key && strstr(key, "explicit:") == key && strstr(key, "left"));
    clone = cxpr_expr_ast_clone(ast);
    assert(clone && strcmp(cxpr_expr_ast_call_instance_id(clone), "left") == 0);
    free(key); free(printed); cxpr_expr_ast_free(clone); cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    parser = cxpr_expr_parser_new(); err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(parser, "audio.voice@right(440)", &err);
    assert(ast && strcmp(cxpr_expr_ast_call_instance_id(ast), "right") == 0);
    printed = cxpr_expr_ast_to_string(ast);
    assert(printed && strcmp(printed, "audio.voice@right(440)") == 0);
    free(printed); cxpr_expr_ast_free(ast); cxpr_expr_parser_free(parser);

    parser = cxpr_expr_parser_new(); err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(parser, "1 |> voice@pipe(2)", &err);
    assert(ast && strcmp(cxpr_expr_ast_call_instance_id(ast), "pipe") == 0);
    printed = cxpr_expr_ast_to_string(ast);
    assert(printed && strcmp(printed, "voice@pipe(1, 2)") == 0);
    free(printed); cxpr_expr_ast_free(ast); cxpr_expr_parser_free(parser);
}

static void test_parser_primary_forms(void) {
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_parser* p = cxpr_expr_parser_new();

    assert(p);

    ast = cxpr_expr_ast_parse(p, "macd(fast=9, slow=21).signal", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_expr_ast_producer_name(ast), "macd") == 0);
    assert(strcmp(cxpr_expr_ast_producer_arg_name(ast, 0), "fast") == 0);
    assert(strcmp(cxpr_expr_ast_producer_arg_name(ast, 1), "slow") == 0);
    assert(strcmp(cxpr_expr_ast_producer_field(ast), "signal") == 0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(
        p,
        "{ range_safe = max(high - low, close * $atr_floor_pct), "
        "close_location = (close - low) / range_safe, "
        "body_return = (close - open) / max(open, close * $atr_floor_pct), "
        "test = close - open / 50, }",
        &err);
    assert(ast);
    assert(err.code == CXPR_OK);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_RECORD);
    assert(cxpr_expr_ast_record_field_count(ast) == 4u);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 3u), "test") == 0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(
        p,
        "{ period = 20 { optimize = [30, 40] }, "
        "exit = { buffer_mult = 0.03 { optimize { min = 0.01, max = 0.05 } } } }",
        &err);
    assert(ast);
    assert(err.code == CXPR_OK);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_RECORD);
    assert(cxpr_expr_ast_record_field_count(ast) == 2u);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 0u), "period") == 0);
    assert(strcmp(cxpr_expr_ast_record_field_name(ast, 1u), "exit") == 0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(p, "body.velocity.x[2]", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_INDEX);
    assert(cxpr_expr_ast_kind_of(cxpr_expr_ast_lookback_target(ast)) == CXPR_NODE_CHAIN_ACCESS);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(p, "\"tf_1h\"", &err);
    assert(ast);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_STRING);
    assert(strcmp(cxpr_expr_ast_string_value(ast), "tf_1h") == 0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(p, "foo = 1", &err);
    assert(ast == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strcmp(err.message, "Unexpected token after expression") == 0);
    cxpr_expr_parser_free(p);

    p = cxpr_expr_parser_new();
    assert(p);
    err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(p, "clclamp(eia,1,os,se,G,se,0,o]s,se,: ", &err);
    assert(ast == NULL);
    assert(err.code != CXPR_OK);
    cxpr_expr_parser_free(p);
}

int main(void) {
    test_explicit_instance_identity();
    test_parser_primary_forms();
    printf("  \xE2\x9C\x93 parser_primary\n");
    return 0;
}
