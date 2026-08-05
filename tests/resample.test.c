#include <cxpr/cxpr.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cxpr_expr_ast* parse(const char* text) {
    cxpr_error err = {0};
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    assert(parser); ast = cxpr_expr_ast_parse(parser, text, &err);
    cxpr_expr_parser_free(parser);
    if (!ast) fprintf(stderr, "parse failed: %s\n", err.message); assert(ast); return ast;
}
static void expect_duration(const char* text, int64_t ns, const char* canonical) {
    cxpr_error err = {0}; cxpr_resample_interval out = {0};
    assert(cxpr_parse_fixed_duration(text, &out, &err));
    assert(out.duration_ns == ns); assert(strcmp(out.canonical, canonical) == 0);
}
static void reject_duration(const char* text) {
    cxpr_error err = {0}; cxpr_resample_interval out = {0};
    assert(!cxpr_parse_fixed_duration(text, &out, &err)); assert(err.code != CXPR_OK);
}
static void test_durations(void) {
    expect_duration("1ns", 1, "1ns"); expect_duration("2us", 2000, "2us");
    expect_duration("500ms", 500000000, "500ms");
    expect_duration("30s", INT64_C(30000000000), "30s");
    expect_duration("5m", INT64_C(300000000000), "5m");
    expect_duration("1h", INT64_C(3600000000000), "1h");
    expect_duration("1d", INT64_C(86400000000000), "1d");
    expect_duration("0005m", INT64_C(300000000000), "5m");
    expect_duration("9223372036854775807ns", INT64_MAX, "9223372036854775807ns");
    reject_duration(NULL); reject_duration(""); reject_duration("0s");
    reject_duration("-1h"); reject_duration("1.5h"); reject_duration("1H");
    reject_duration("1mo"); reject_duration("1w");
    reject_duration("9223372036854775808ns"); reject_duration("9223372036854775807d");
}
static void expect_call(const char* text) {
    cxpr_error err = {0}; cxpr_resample_call call = {0}; cxpr_expr_ast* ast = parse(text);
    assert(cxpr_resample_call_parse(ast, &call, &err));
    assert(strcmp(cxpr_expr_ast_identifier_name(call.source), "close") == 0);
    assert(call.every.duration_ns == INT64_C(3600000000000));
    assert(strcmp(call.every.canonical, "1h") == 0); cxpr_expr_ast_free(ast);
}
static void reject_call(const char* text, cxpr_error_code code) {
    cxpr_error err = {0}; cxpr_resample_call call = {0}; cxpr_expr_ast* ast = parse(text);
    assert(!cxpr_resample_call_parse(ast, &call, &err)); assert(err.code == code);
    cxpr_expr_ast_free(ast);
}
static void test_calls(void) {
    expect_call("resample(close, \"1h\")");
    expect_call("resample(close, every=\"1h\")");
    reject_call("resample(close)", CXPR_ERR_WRONG_ARITY);
    reject_call("resample(close, interval=\"1h\")", CXPR_ERR_SYNTAX);
    reject_call("resample(source=close, every=\"1h\")", CXPR_ERR_SYNTAX);
    reject_call("resample(close, every=period)", CXPR_ERR_TYPE_MISMATCH);
    reject_call("resample(close, every=\"0h\")", CXPR_ERR_SYNTAX);
    reject_call("resample(close, every=\"1mo\")", CXPR_ERR_SYNTAX);
    reject_call("resample(1 + 2, every=\"1h\")", CXPR_ERR_TYPE_MISMATCH);
}

static void test_argument_spans_and_diagnostics(void) {
    const char* text = "resample(close, every=\"1h\")";
    cxpr_expr_ast* ast = parse(text);
    const cxpr_expr_ast* source = cxpr_expr_ast_call_arg(ast, 0u);
    const cxpr_expr_ast* interval = cxpr_expr_ast_call_arg(ast, 1u);
    cxpr_source_span source_span = {0};
    cxpr_source_span interval_span = {0};
    cxpr_expr_ast* invalid;
    cxpr_resample_call call = {0};
    cxpr_error err = {0};

    assert(cxpr_expr_ast_source_span(source, &source_span));
    assert(cxpr_expr_ast_source_span(interval, &interval_span));
    assert(source_span.start.offset == 9u && source_span.end.offset == 14u);
    assert(interval_span.start.offset == 22u && interval_span.end.offset == 26u);
    assert(source_span.start.line == 1u && source_span.start.column == 9u);
    assert(interval_span.start.line == 1u && interval_span.start.column == 22u);
    assert(source_span.end.offset <= interval_span.start.offset);
    cxpr_expr_ast_free(ast);

    invalid = parse("resample(close, every=period)");
    assert(!cxpr_resample_call_parse(invalid, &call, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    assert(err.position == 22u && err.line == 1u && err.column == 22u);
    cxpr_expr_ast_free(invalid);

    invalid = parse("resample(1 + 2, every=\"1h\")");
    assert(!cxpr_resample_call_parse(invalid, &call, &err));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    assert(err.position == 9u && err.line == 1u && err.column == 9u);
    cxpr_expr_ast_free(invalid);
}

static void test_model_semantic_validation(void) {
    static const struct { const char* expression; cxpr_error_code code; } cases[] = {
        {"resample()", CXPR_ERR_WRONG_ARITY},
        {"resample(close)", CXPR_ERR_WRONG_ARITY},
        {"resample(close, $period)", CXPR_ERR_TYPE_MISMATCH},
        {"resample(close, every=\"1H\")", CXPR_ERR_SYNTAX},
        {"resample(close, every=\"1mo\")", CXPR_ERR_SYNTAX},
        {"resample(close + 1, every=\"1h\")", CXPR_ERR_TYPE_MISMATCH},
        {"resample(close, \"1h\")[-1]", CXPR_ERR_INVALID_INDEX},
        {"resample(close, \"1h\")[0.5]", CXPR_ERR_INVALID_INDEX},
        {"resample(close, \"1h\")[1e30]", CXPR_ERR_INVALID_INDEX},
        {"resample(close, \"1h\")[1e309]", CXPR_ERR_INVALID_INDEX},
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[256]; cxpr_error err = {0}; cxpr_model* model;
        snprintf(source, sizeof(source),
                 "model invalid_resample\nin { close, period }\nout value = %s\n",
                 cases[i].expression);
        model = cxpr_model_parse(source, &err);
        if (!model) fprintf(stderr, "invalid fixture %zu did not parse: %s\n", i, err.message);
        assert(model);
        assert(!cxpr_model_validate(model, &err)); assert(err.code == cases[i].code);
        assert(err.position > 0u); assert(err.line > 0u);
        cxpr_model_free(model);
    }
}

static void test_alias_preserves_resample(void) {
    const cxpr_alias aliases[] = {{"hourly", "resample(close, every=\"1h\")"}};
    char* expanded = NULL; cxpr_error err = {0};
    assert(cxpr_expand_aliases("hourly[1]", aliases, 1u, &expanded, &err) == 1);
    assert(strstr(expanded, "resample(close, every=\"1h\")") != NULL);
    free(expanded);
}

static void test_analysis_treats_resample_as_core_transform(void) {
    cxpr_error err = {0}; cxpr_analysis analysis = {0};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_expr_ast* ast = parse("resample(close, every=\"1h\")");
    assert(registry); assert(cxpr_analyze(ast, registry, &analysis, &err));
    assert(analysis.result_type == CXPR_EXPR_NUMBER);
    cxpr_expr_ast_free(ast); cxpr_registry_free(registry);
}

int main(void) {
    test_durations(); test_calls(); test_argument_spans_and_diagnostics();
    test_model_semantic_validation();
    test_alias_preserves_resample();
    test_analysis_treats_resample_as_core_transform();
    puts("resample syntax tests passed"); return 0;
}
