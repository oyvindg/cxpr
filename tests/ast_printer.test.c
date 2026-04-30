#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // IWYU pragma: keep

static char* render_expr(const char* expr) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    char* text;

    assert(parser);
    ast = cxpr_parse(parser, expr, &err);
    assert(ast);
    text = cxpr_ast_to_string(ast);
    assert(text);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    return text;
}

static void assert_renders(const char* expr, const char* expected) {
    char* text = render_expr(expr);
    assert(strcmp(text, expected) == 0);
    free(text);
}

static void assert_round_trips(const char* expr) {
    char* first = render_expr(expr);
    char* second = render_expr(first);

    assert(strcmp(first, second) == 0);
    free(first);
    free(second);
}

static void test_literals_round_trip(void) {
    assert_renders("3.14", "3.14");
    assert_renders("true", "true");
    assert_renders("false", "false");
    assert_renders("\"tf_1h\"", "\"tf_1h\"");
    assert_renders("\"a\\\"b\"", "\"a\\\"b\"");
    assert_round_trips("123.456");
    assert_round_trips("\"abc\"");
}

static void test_operator_precedence(void) {
    assert_renders("a + b * c", "a + b * c");
    assert_renders("(a + b) * c", "(a + b) * c");
    assert_renders("a - (b - c)", "a - (b - c)");
    assert_renders("(a ^ b) ^ c", "(a ^ b) ^ c");
    assert_renders("a ^ b ^ c", "a ^ b ^ c");
}

static void test_nested_boolean_expression(void) {
    assert_renders("a > 0 and b < 1 or not c", "a > 0 and b < 1 or not c");
    assert_round_trips("not (a > 0 and b < 1)");
}

static void test_named_args_and_lookback(void) {
    assert_renders("ema(period=14)", "ema(period=14)");
    assert_renders("close[1]", "close[1]");
    assert_renders("macd(12, 26, 9).signal[2]", "macd(12, 26, 9).signal[2]");
    assert_renders("macd(fast=12, slow=26, signal=9).signal[2]",
                   "macd(fast=12, slow=26, signal=9).signal[2]");
}

static void test_ternary(void) {
    assert_renders("x > 0 ? x : -x", "x > 0 ? x : -x");
    assert_renders("(a ? b : c) ? d : e", "(a ? b : c) ? d : e");
    assert_renders("a ? (b ? c : d) : e", "a ? (b ? c : d) : e");
}

static void test_pipe_round_trip_desugars(void) {
    assert_renders("x |> clamp(0, 1)", "clamp(x, 0, 1)");
    assert_round_trips("x |> clamp(0, 1)");
}

static void test_special_numbers(void) {
    cxpr_ast* nan_ast = cxpr_ast_new_number(NAN);
    cxpr_ast* inf_ast = cxpr_ast_new_number(INFINITY);
    cxpr_ast* neg_inf_ast = cxpr_ast_new_number(-INFINITY);
    char* nan_text;
    char* inf_text;
    char* neg_inf_text;

    assert(nan_ast);
    assert(inf_ast);
    assert(neg_inf_ast);
    nan_text = cxpr_ast_to_string(nan_ast);
    inf_text = cxpr_ast_to_string(inf_ast);
    neg_inf_text = cxpr_ast_to_string(neg_inf_ast);
    assert(nan_text && strcmp(nan_text, "nan()") == 0);
    assert(inf_text && strcmp(inf_text, "inf()") == 0);
    assert(neg_inf_text && strcmp(neg_inf_text, "-inf()") == 0);
    free(nan_text);
    free(inf_text);
    free(neg_inf_text);
    cxpr_ast_free(nan_ast);
    cxpr_ast_free(inf_ast);
    cxpr_ast_free(neg_inf_ast);
}

static void test_deep_nesting_round_trip(void) {
    const char* expr = "(((((((((((((((((((((x + 1)))))))))))))))))))))";
    assert_round_trips(expr);
}

int main(void) {
    test_literals_round_trip();
    test_operator_precedence();
    test_nested_boolean_expression();
    test_named_args_and_lookback();
    test_ternary();
    test_pipe_round_trip_desugars();
    test_special_numbers();
    test_deep_nesting_round_trip();
    printf("  \xE2\x9C\x93 ast_printer\n");
    return 0;
}
