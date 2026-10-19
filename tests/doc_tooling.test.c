#include <cxpr/doc_tooling.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char baseline_json[] =
    "{\"ok\":true,\"outline\":[{\"kind\":\"model\",\"name\":\"demo\",\"value\":\"\","
    "\"startLine\":0,\"startChar\":0,\"endLine\":0,\"endChar\":10},{\"kind\":\"input\","
    "\"name\":\"close\",\"value\":\"\",\"startLine\":1,\"startChar\":0,\"endLine\":1,"
    "\"endChar\":8},{\"kind\":\"output\",\"name\":\"result\",\"value\":\"\","
    "\"startLine\":2,\"startChar\":0,\"endLine\":2,\"endChar\":18}],\"folds\":[{"
    "\"startLine\":0,\"startChar\":0,\"endLine\":3,\"endChar\":0}],\"requirements\":[],\"tokens\":[{\"line\":0,"
    "\"start\":6,\"length\":4,\"type\":\"assignment\"},{\"line\":1,\"start\":3,\"length\":5,"
    "\"type\":\"input\"},{\"line\":2,\"start\":4,\"length\":6,\"type\":\"assignment\"}],"
    "\"sourceTokens\":[{\"line\":0,\"start\":0,\"length\":5,\"type\":\"keyword\"},{"
    "\"line\":0,\"start\":6,\"length\":4,\"type\":\"reference\"},{\"line\":1,\"start\":0,"
    "\"length\":2,\"type\":\"keyword\"},{\"line\":1,\"start\":3,\"length\":5,\"type\":"
    "\"input\"},{\"line\":2,\"start\":0,\"length\":3,\"type\":\"keyword\"},{\"line\":2,"
    "\"start\":4,\"length\":6,\"type\":\"assignment\"},{\"line\":2,\"start\":11,\"length\":1,"
    "\"type\":\"assignment\"},{\"line\":2,\"start\":13,\"length\":5,\"type\":\"input\"}],"
    "\"functions\":[],\"diagnostics\":[]}\n";

static char* read_fixture(void) {
    const char* path = CXPR_TEST_SOURCE_DIR "/fixtures/doc_tooling_markers.cxpr";
    FILE* file = fopen(path, "rb");
    long size;
    char* source;
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

static char* strip_markers(char* source,
                           const char* selected,
                           size_t* out_line,
                           size_t* out_column) {
    static const char* markers[] = {"/*@resolve*/", "/*@complete*/"};
    size_t selected_offset = 0u;
    int selected_found = 0;
    for (size_t marker_index = 0u; marker_index < 2u; ++marker_index) {
        const char* marker = markers[marker_index];
        char* hit = strstr(source, marker);
        assert(hit != NULL);
        if (strcmp(marker, selected) == 0) {
            selected_offset = (size_t)(hit - source);
            selected_found = 1;
        }
        memmove(hit, hit + strlen(marker), strlen(hit + strlen(marker)) + 1u);
        if (selected_found && (size_t)(hit - source) < selected_offset) {
            selected_offset -= strlen(marker);
        }
    }
    assert(selected_found);
    *out_line = 0u;
    *out_column = 0u;
    for (size_t i = 0u; i < selected_offset; ++i) {
        if (source[i] == '\n') {
            (*out_line)++;
            *out_column = 0u;
        } else {
            (*out_column)++;
        }
    }
    return source;
}

static void test_analysis_baseline(void) {
    const char* source = "model demo\nin close\nout result = close\n";
    char* json = cxpr_document_analyze_json(source, "baseline.cxpr");
    assert(json != NULL);
    assert(strcmp(json, baseline_json) == 0);
    cxpr_document_json_free(json);
}

static void test_registry_signature_fields(void) {
    const char* source = "model demo\nin close\nout result = sqrt(close)\n";
    char* json = cxpr_document_analyze_json(source, "signature.cxpr");
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"sqrt\"") != NULL);
    assert(strstr(json, "\"builtin\":true") != NULL);
    assert(strstr(json, "\"signature\":\"sqrt(") != NULL);
    cxpr_document_json_free(json);
}

static void test_marker_resolution(void) {
    size_t line;
    size_t column;
    char* source = strip_markers(read_fixture(), "/*@resolve*/", &line, &column);
    char* json = cxpr_document_resolve_json(
        source, "doc_tooling_markers.cxpr", line, column);
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"close\"") != NULL);
    assert(strstr(json, "\"kind\":\"input\"") != NULL);
    assert(strstr(json, "\"definition\":") != NULL);
    assert(strstr(json, "\"startLine\":1,\"startChar\":3,\"endLine\":1,\"endChar\":8") != NULL);
    cxpr_document_json_free(json);
    free(source);
}

static void test_model_resolution_has_callable_metadata(void) {
    const char* source =
        "# Exponential Moving Average.\n"
        "# Stateful streaming implementation.\n"
        "model ema {\n  type = \"indicator\"\n}\n"
        "in {\n  source\n  $period = 20\n}\n"
        "out value {\n  description = \"EMA\"\n}\n"
        "probe = ema\n";
    char* json = cxpr_document_resolve_json(source, "ema.cxpr", 12u, 9u);
    assert(json != NULL);
    assert(strstr(json, "\"kind\":\"function\"") != NULL);
    assert(strstr(json, "\"signature\":\"ema(source, period)\"") != NULL);
    assert(strstr(json, "\"documentation\":\"Exponential Moving Average. Stateful streaming implementation.\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_binding_resolution_includes_expression(void) {
    const char* source =
        "model market_structure\n"
        "in { $left = 2, $right = 2 }\n"
        "left = max(1, $left)\n"
        "right = max(1, $right)\n"
        "pivot_window = ts_window_span(left, right)\n"
        "out value = pivot_window\n";
    char* json = cxpr_document_resolve_json(
        source, "market_structure.cxpr", 5u, 13u);
    assert(json != NULL);
    assert(strstr(json, "\"kind\":\"binding\"") != NULL);
    assert(strstr(json,
                  "\"signature\":\"pivot_window = ts_window_span(left, right)\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_parameter_resolution_preserves_dollar_prefix(void) {
    const char* source =
        "model demo\n"
        "$slow_period = 26\n"
        "out value = $slow_period\n";
    char* json = cxpr_document_resolve_json(source, "parameter-hover.cxpr", 2u, 14u);
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"$slow_period\"") != NULL);
    assert(strstr(json, "\"signature\":\"$slow_period = 26\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_marker_completion(void) {
    size_t line;
    size_t column;
    char* source = strip_markers(read_fixture(), "/*@complete*/", &line, &column);
    char* json = cxpr_document_complete_json(
        source, "doc_tooling_markers.cxpr", line, column);
    assert(json != NULL);
    assert(strstr(json, "\"prefix\":\"sq\"") != NULL);
    assert(strstr(json, "\"name\":\"sqrt\"") != NULL);
    assert(strstr(json, "\"signature\":\"sqrt(") != NULL);
    cxpr_document_json_free(json);
    free(source);
}

static void test_registry_catalog(void) {
    char* json = cxpr_document_query_functions_json();
    assert(json != NULL);
    assert(json[0] == '[');
    assert(strstr(json, "\"name\":\"sqrt\"") != NULL);
    assert(strstr(json, "\"signature\":\"sqrt(") != NULL);
    assert(strstr(json, "\"documentation\":\"Returns the square root.\"") != NULL);
    assert(strstr(json, "\"name\":\"avg\"") != NULL);
    assert(strstr(json, "\"name\":\"window\"") != NULL);
    assert(strstr(json, "__cxpr_window_sum") == NULL);
    cxpr_document_json_free(json);
}

static void test_host_builtin_catalog_merge(void) {
    static const char* const params[] = {"source", "period"};
    static const cxpr_function_info host_builtins[] = {{
        "host_moving_average",
        "host_moving_average(source, period)",
        "Host-owned moving average metadata.",
        params,
        2u,
        2u,
        2u,
        CXPR_VALUE_NUMBER,
        0u
    }};
    const char* source = "model demo\nout value = host_moving_average(input, 20)\n";
    char* functions = cxpr_document_query_functions_with_builtins_json(
        host_builtins, 1u);
    char* completion = cxpr_document_complete_with_builtins_json(
        "host_mov", "host.cxpr", 0u, 8u, host_builtins, 1u);
    char* hover = cxpr_document_resolve_with_builtins_json(
        source, "host.cxpr", 1u, 18u, host_builtins, 1u);
    assert(functions != NULL && completion != NULL && hover != NULL);
    assert(strstr(functions, "\"name\":\"host_moving_average\"") != NULL);
    assert(strstr(completion, "\"signature\":\"host_moving_average(source, period)\"") != NULL);
    assert(strstr(hover, "\"documentation\":\"Host-owned moving average metadata.\"") != NULL);
    cxpr_document_json_free(functions);
    cxpr_document_json_free(completion);
    cxpr_document_json_free(hover);
}

static void test_source_tokens_and_completion_contract(void) {
    const char* source =
        "model demo\n# note\nvalue = max(1, length = 2)\nout value\n";
    char* json = cxpr_document_analyze_json(source, "tokens.cxpr");
    assert(json != NULL);
    assert(strstr(json, "\"sourceTokens\":[") != NULL);
    assert(strstr(json, "\"type\":\"comment\"") != NULL);
    assert(strstr(json, "\"type\":\"punctuation\"") != NULL);
    assert(strstr(json, "\"start\":15,\"length\":6,\"type\":\"namedArgument\"") != NULL);
    cxpr_document_json_free(json);

    json = cxpr_document_complete_json(source, "tokens.cxpr", 0u, 1u);
    assert(json != NULL);
    assert(strstr(json, "\"prefix\":\"m\"") != NULL);
    assert(strstr(json, "\"name\":\"model\",\"kind\":\"keyword\"") != NULL);
    assert(strstr(json, "\"name\":\"max\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_model_name_semantic_token(void) {
    const char* source =
        "model moving_average_demo {\n"
        "    type = \"strategy\"\n"
        "}\n";
    char* json = cxpr_document_analyze_json(source, "model-token.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":6,\"length\":19,\"type\":\"assignment\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":4,\"length\":4,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":9,\"length\":1,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":26,\"length\":8,\"type\":\"property\"}") == NULL);
    cxpr_document_json_free(json);
}

static void test_output_metadata_is_named_argument(void) {
    const char* source =
        "model demo\n"
        "out latest_low {\n"
        "    label = \"Latest Swing Low\"\n"
        "    description = \"Latest causally confirmed swing low.\"\n"
        "}\n"
        "out current_midpoint { label = \"Current Structure Midpoint\" }\n";
    char* json = cxpr_document_analyze_json(source, "output-metadata.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":4,\"length\":5,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":10,\"length\":1,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":4,\"length\":11,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":5,\"start\":23,\"length\":5,\"type\":\"namedArgument\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_import_tokens_are_individual(void) {
    const char* source = "use pivots, window from ts\n";
    char* json = cxpr_document_analyze_json(source, "imports.cxpr");
    assert(json != NULL);
    assert(strstr(json, "\"start\":4,\"length\":6,\"type\":\"import\"") != NULL);
    assert(strstr(json, "\"start\":12,\"length\":6,\"type\":\"import\"") != NULL);
    assert(strstr(json, "\"start\":24,\"length\":2,\"type\":\"importPath\"") != NULL);
    assert(strstr(json, "\"start\":4,\"length\":22,\"type\":\"import\"") == NULL);
    cxpr_document_json_free(json);
}

static void test_direct_import_path_is_one_token(void) {
    const char* source = "use indicators/bollinger\n";
    char* json = cxpr_document_analyze_json(source, "direct-import.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":4,\"length\":20,\"type\":\"importPath\"}") != NULL);
    assert(strstr(json, "\"start\":14,\"length\":1,\"type\":\"operator\"") == NULL);
    cxpr_document_json_free(json);
}

static void test_exported_and_internal_parameter_tokens(void) {
    const char* source =
        "model demo\n"
        "in { $exported = 123 }\n"
        "$internal = 456\n"
        "exported = max(1, $exported)\n"
        "out value = exported + $exported + $internal\n";
    char* json = cxpr_document_analyze_json(source, "inline-input.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":5,\"length\":9,\"type\":\"input\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":0,\"length\":9,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":18,\"length\":9,\"type\":\"input\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":12,\"length\":8,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":23,\"length\":9,\"type\":\"input\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":35,\"length\":9,\"type\":\"parameter\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_unknown_symbol_diagnostic_location(void) {
    const char* source =
        "model demo\n"
        "in lower\n"
        "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
        "range = xupper - lower\n";
    char* json = cxpr_document_analyze_json(source, "diagnostic-location.cxpr");
    assert(json != NULL);
    assert(strstr(json, "Expression references unknown symbol 'xupper'") != NULL);
    assert(strstr(json, "\"line\":19,\"column\":8") != NULL);
    cxpr_document_json_free(json);
}

static void test_unknown_symbol_location_ignores_identifier_prefixes(void) {
    const char* source =
        "model demo\n"
        "fast_avgx = 1\n"
        "signal = fast_avg > 0\n";
    char* json = cxpr_document_analyze_json(source, "diagnostic-prefix.cxpr");
    assert(json != NULL);
    assert(strstr(json, "Expression references unknown symbol 'fast_avg'") != NULL);
    assert(strstr(json, "\"line\":2,\"column\":9") != NULL);
    cxpr_document_json_free(json);
}

static void test_runtime_symbol_in_parameter_reports_source_location(void) {
    const char* source =
        "model demo\n"
        "\n\n\n\n\n\n\n\n"
        "in close, $xxx=rrrx\n";
    char* json = cxpr_document_analyze_json(source, "moving_average_demo.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "Constant expression references runtime symbol 'rrrx'") != NULL);
    assert(strstr(json, "\"line\":9,\"column\":15") != NULL);
    cxpr_document_json_free(json);
}

static void test_window_syntax_hover_metadata(void) {
    const char* source =
        "model demo\n"
        "in close\n"
        "deviation = stddev(window(close, 20))\n";
    char* stddev = cxpr_document_resolve_json(
        source, "window-hover.cxpr", 2u, 14u);
    char* window = cxpr_document_resolve_json(
        source, "window-hover.cxpr", 2u, 21u);
    assert(stddev != NULL);
    assert(window != NULL);
    assert(strstr(stddev, "\"kind\":\"function\"") != NULL);
    assert(strstr(stddev, "\"signature\":\"stddev(window(expression, period))\"") != NULL);
    assert(strstr(stddev, "\"documentation\":\"Computes the standard deviation") != NULL);
    assert(strstr(window, "\"kind\":\"function\"") != NULL);
    assert(strstr(window, "\"signature\":\"window(expression, period)\"") != NULL);
    assert(strstr(window, "\"documentation\":\"Creates a compile-time trailing expression window") != NULL);
    cxpr_document_json_free(stddev);
    cxpr_document_json_free(window);
}

static void test_variadic_max_hover_metadata(void) {
    const char* source = "model demo\nvalue = max(1, 2)\n";
    char* json = cxpr_document_resolve_json(source, "max-hover.cxpr", 1u, 9u);
    assert(json != NULL);
    assert(strstr(json, "\"signature\":\"max(value, ...values)\"") != NULL);
    assert(strstr(json, "\"minArgs\":1,\"maxArgs\":8") != NULL);
    assert(strstr(json, "Accepts 1 to 8 numeric arguments") != NULL);
    assert(strstr(json, "max(window(expression, period))") != NULL);
    cxpr_document_json_free(json);
}

static void test_cross_below_hover_metadata(void) {
    const char* source =
        "model demo\n"
        "in fast, slow\n"
        "out signal = cross_below(fast, slow)\n";
    char* json = cxpr_document_resolve_json(source, "cross-hover.cxpr", 2u, 17u);
    assert(json != NULL);
    assert(strstr(json, "\"kind\":\"function\"") != NULL);
    assert(strstr(json, "\"type\":\"bool\"") != NULL);
    assert(strstr(json, "\"minArgs\":2,\"maxArgs\":2") != NULL);
    assert(strstr(json, "\"signature\":\"cross_below(left, right)\"") != NULL);
    assert(strstr(json,
                  "\"documentation\":\"Returns true when left crosses from above to below right.\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_stacked_builtin_and_anchor_token_boundary(void) {
    const char* source =
        "model demo\n"
        "in e_fast, e_slow, e_anchor\n"
        "ema_stack = stacked(e_fast, e_slow, e_anchor)\n";
    char* analysis = cxpr_document_analyze_json(source, "stacked.cxpr");
    char* hover = cxpr_document_resolve_json(source, "stacked.cxpr", 2u, 14u);
    assert(analysis != NULL);
    assert(hover != NULL);
    assert(strstr(analysis, "unknown function 'stacked'") == NULL);
    assert(strstr(analysis,
                  "{\"line\":2,\"start\":36,\"length\":8,\"type\":\"input\"}") != NULL);
    assert(strstr(analysis,
                  "{\"line\":2,\"start\":41,\"length\":2,\"type\":\"keyword\"}") == NULL);
    assert(strstr(hover,
                  "\"signature\":\"stacked(first, second, ...values)\"") != NULL);
    assert(strstr(hover, "\"type\":\"bool\"") != NULL);
    cxpr_document_json_free(analysis);
    cxpr_document_json_free(hover);
}

static void test_resample_is_valid_tooling_function(void) {
    const char* source =
        "model demo\n"
        "in close\n"
        "hourly = resample(close, every=\"1h\")\n"
        "out hourly\n";
    char* analysis = cxpr_document_analyze_json(source, "resample.cxpr");
    char* hover = cxpr_document_resolve_json(source, "resample.cxpr", 2u, 12u);
    assert(analysis != NULL);
    assert(hover != NULL);
    assert(strstr(analysis, "unknown function 'resample'") == NULL);
    assert(strstr(analysis, "\"diagnostics\":[]") != NULL);
    assert(strstr(analysis,
                  "\"requirements\":[{\"kind\":\"resample\",\"source\":\"close\","
                  "\"interval\":\"1h\",\"durationNs\":\"3600000000000\","
                  "\"resultType\":\"number\"") != NULL);
    assert(strstr(analysis,
                  "\"range\":{\"startLine\":2,\"startChar\":9,"
                  "\"endLine\":2,\"endChar\":17}") != NULL);
    assert(strstr(hover, "\"signature\":\"resample(source, every)\"") != NULL);
    cxpr_document_json_free(analysis);
    cxpr_document_json_free(hover);
}

static void test_metadata_min_max_are_not_builtin_functions(void) {
    const char* source =
        "model demo\n"
        "in {\n"
        "  $period = 20 { min = 1, max = 512 }\n"
        "}\n";
    char* minimum = cxpr_document_resolve_json(source, "metadata.cxpr", 2u, 19u);
    char* maximum = cxpr_document_resolve_json(source, "metadata.cxpr", 2u, 28u);
    assert(minimum != NULL);
    assert(maximum != NULL);
    assert(strstr(minimum, "\"builtin\":true") == NULL);
    assert(strstr(maximum, "\"builtin\":true") == NULL);
    assert(strstr(minimum, "\"kind\":\"function\"") == NULL);
    assert(strstr(maximum, "\"kind\":\"function\"") == NULL);
    cxpr_document_json_free(minimum);
    cxpr_document_json_free(maximum);
}

static void test_unresolved_resolution_exposes_c_parsed_imports(void) {
    const char* source = "use pivots, window from ts\nvalue = ts_window_span(1, 2)\n";
    char* json = cxpr_document_resolve_json(source, "imports.cxpr", 1u, 10u);
    assert(json != NULL);
    assert(strstr(json, "\"kind\":\"unresolved\"") != NULL);
    assert(strstr(json, "\"imports\":[\"ts/pivots\",\"ts/window\"]") != NULL);
    cxpr_document_json_free(json);
}

static void test_operator_token_does_not_match_identifier_substring(void) {
    const char* source =
        "model demo\n"
        "peak_floor = 1\n"
        "signal = peak_floor > 0 or close > 0\n";
    char* json = cxpr_document_analyze_json(source, "operator-boundary.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":17,\"length\":2,\"type\":\"keyword\"}") == NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":24,\"length\":2,\"type\":\"keyword\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_multiline_boolean_operators_are_keywords(void) {
    const char* source =
        "model demo\n"
        "in history_ok, trend_entry_basket, ltf, opening_breakaway\n"
        "normal_entry_basket =\n"
        "    history_ok\n"
        "    and ((trend_entry_basket and not ltf.reentry_sell_pressure) or opening_breakaway)\n";
    char* json = cxpr_document_analyze_json(source, "multiline-boolean.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":4,\"length\":3,\"type\":\"keyword\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":29,\"length\":3,\"type\":\"keyword\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":33,\"length\":3,\"type\":\"keyword\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":64,\"length\":2,\"type\":\"keyword\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_comma_separated_inputs_are_source_input_tokens(void) {
    const char* source = "in open, high, low, close, volume\n";
    char* json = cxpr_document_analyze_json(source, "flat-inputs.cxpr");
    static const char* expected[] = {
        "{\"line\":0,\"start\":3,\"length\":4,\"type\":\"input\"}",
        "{\"line\":0,\"start\":9,\"length\":4,\"type\":\"input\"}",
        "{\"line\":0,\"start\":15,\"length\":3,\"type\":\"input\"}",
        "{\"line\":0,\"start\":20,\"length\":5,\"type\":\"input\"}",
        "{\"line\":0,\"start\":27,\"length\":6,\"type\":\"input\"}"
    };
    assert(json != NULL);
    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(strstr(json, expected[i]) != NULL);
    }
    cxpr_document_json_free(json);
}

static void test_optimize_block_is_named_argument(void) {
    const char* source =
        "model demo\n"
        "stddev = 2.0 { optimize { min = 2.0, max = 2.4, step = 0.2 } }\n";
    char* json = cxpr_document_analyze_json(source, "optimize-token.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":15,\"length\":8,\"type\":\"namedArgument\"}") != NULL);
    cxpr_document_json_free(json);
}


int main(void) {
    test_analysis_baseline();
    test_registry_signature_fields();
    test_marker_resolution();
    test_model_resolution_has_callable_metadata();
    test_binding_resolution_includes_expression();
    test_parameter_resolution_preserves_dollar_prefix();
    test_marker_completion();
    test_registry_catalog();
    test_host_builtin_catalog_merge();
    test_source_tokens_and_completion_contract();
    test_model_name_semantic_token();
    test_output_metadata_is_named_argument();
    test_import_tokens_are_individual();
    test_direct_import_path_is_one_token();
    test_exported_and_internal_parameter_tokens();
    test_unknown_symbol_diagnostic_location();
    test_unknown_symbol_location_ignores_identifier_prefixes();
    test_runtime_symbol_in_parameter_reports_source_location();
    test_window_syntax_hover_metadata();
    test_variadic_max_hover_metadata();
    test_cross_below_hover_metadata();
    test_stacked_builtin_and_anchor_token_boundary();
    test_resample_is_valid_tooling_function();
    test_metadata_min_max_are_not_builtin_functions();
    test_unresolved_resolution_exposes_c_parsed_imports();
    test_operator_token_does_not_match_identifier_substring();
    test_multiline_boolean_operators_are_keywords();
    test_comma_separated_inputs_are_source_input_tokens();
    test_optimize_block_is_named_argument();
    puts("doc tooling tests passed");
    return 0;
}
