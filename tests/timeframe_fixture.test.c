#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(void) {
    char path[1024];
    FILE* file;
    long size;
    char* source;

    snprintf(path, sizeof(path), "%s/fixtures/timeframes/multi_domain.cxpr",
             CXPR_TEST_SOURCE_DIR);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    assert(size >= 0);
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    assert(source != NULL);
    assert(fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);
    return source;
}

static void assert_resample_binding(const cxpr_model* model,
                                    size_t binding_index,
                                    const char* binding_name,
                                    const char* source_name,
                                    const char* interval) {
    const cxpr_expr_ast* expr = cxpr_model_binding_expr(model, binding_index);
    const cxpr_expr_ast* source;
    const cxpr_expr_ast* interval_arg;

    assert(strcmp(cxpr_model_binding_name(model, binding_index), binding_name) == 0);
    assert(cxpr_expr_ast_kind_of(expr) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_expr_ast_call_name(expr), "resample") == 0);
    assert(cxpr_expr_ast_call_arg_count(expr) == 2u);
    source = cxpr_expr_ast_call_arg(expr, 0u);
    interval_arg = cxpr_expr_ast_call_arg(expr, 1u);
    assert(cxpr_expr_ast_kind_of(source) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_expr_ast_identifier_name(source), source_name) == 0);
    assert(cxpr_expr_ast_kind_of(interval_arg) == CXPR_NODE_STRING);
    assert(strcmp(cxpr_expr_ast_string_value(interval_arg), interval) == 0);
}

static void assert_resample_lookback_binding(const cxpr_model* model,
                                             size_t binding_index,
                                             const char* binding_name,
                                             const char* source_name,
                                             const char* interval,
                                             double lookback) {
    const cxpr_expr_ast* expr = cxpr_model_binding_expr(model, binding_index);
    const cxpr_expr_ast* target;
    const cxpr_expr_ast* index;

    assert(strcmp(cxpr_model_binding_name(model, binding_index), binding_name) == 0);
    assert(cxpr_expr_ast_kind_of(expr) == CXPR_NODE_INDEX);
    target = cxpr_expr_ast_index_target(expr);
    index = cxpr_expr_ast_index_expression(expr);
    assert(cxpr_expr_ast_kind_of(target) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_expr_ast_call_name(target), "resample") == 0);
    assert(cxpr_expr_ast_call_arg_count(target) == 2u);
    assert(strcmp(cxpr_expr_ast_identifier_name(
                      cxpr_expr_ast_call_arg(target, 0u)), source_name) == 0);
    assert(strcmp(cxpr_expr_ast_string_value(
                      cxpr_expr_ast_call_arg(target, 1u)), interval) == 0);
    assert(cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER);
    assert(cxpr_expr_ast_number_value(index) == lookback);
}

int main(void) {
    cxpr_error err = {0};
    char* source = read_fixture();
    cxpr_model* model = cxpr_model_parse(source, &err);

    free(source);
    if (!model) {
        fprintf(stderr, "timeframe fixture parse failed: %s\n",
                err.message ? err.message : "unknown error");
    }
    assert(model != NULL);
    assert(cxpr_model_binding_count(model) == 14u);
    assert_resample_binding(model, 0u, "hourly_close", "close", "1h");
    assert(strcmp(cxpr_expr_ast_call_arg_name(
                      cxpr_model_binding_expr(model, 0u), 1u), "every") == 0);
    assert_resample_binding(model, 1u, "daily_close", "close", "1d");
    assert_resample_binding(model, 2u, "hourly_temperature", "temperature", "1h");
    assert_resample_binding(model, 3u, "five_minute_requests", "requests", "5m");
    assert_resample_lookback_binding(
        model, 4u, "previous_hourly_close", "close", "1h", 1.0);
    assert_resample_lookback_binding(
        model, 5u, "previous_hourly_temperature", "temperature", "1h", 1.0);
    assert(strcmp(cxpr_model_binding_name(model, 6u),
                  "previous_hourly_close_derived") == 0);
    assert(cxpr_expr_ast_kind_of(cxpr_model_binding_expr(model, 6u)) == CXPR_NODE_INDEX);
    assert_resample_lookback_binding(
        model, 7u, "path_500ms_ago", "path", "500ms", 1.0);
    assert_resample_lookback_binding(
        model, 8u, "path_1s_ago", "path", "500ms", 2.0);
    {
        const cxpr_expr_ast* nested = cxpr_model_binding_expr(model, 13u);
        const cxpr_expr_ast* selected;
        assert(strcmp(cxpr_model_binding_name(model, 13u),
                      "nested_hourly_close_rsi") == 0);
        assert(cxpr_expr_ast_kind_of(nested) == CXPR_NODE_FUNCTION_CALL);
        assert(strcmp(cxpr_expr_ast_call_name(nested), "rsi") == 0);
        selected = cxpr_expr_ast_call_arg(nested, 0u);
        assert(cxpr_expr_ast_kind_of(selected) == CXPR_NODE_FUNCTION_CALL);
        assert(strcmp(cxpr_expr_ast_call_name(selected), "resample") == 0);
        assert(strcmp(cxpr_expr_ast_call_arg_name(selected, 1u), "every") == 0);
    }

    cxpr_model_free(model);
    puts("resample multi-domain syntax fixture OK");
    return 0;
}
