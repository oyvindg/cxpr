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
    "\"startLine\":2,\"startChar\":0,\"endLine\":2,\"endChar\":18}],\"modelType\":\"\",\"folds\":[{"
    "\"startLine\":0,\"startChar\":0,\"endLine\":3,\"endChar\":0}],\"requirements\":[],\"tokens\":[{\"line\":0,"
    "\"start\":6,\"length\":4,\"type\":\"model\"},{\"line\":1,\"start\":3,\"length\":5,"
    "\"type\":\"input\"},{\"line\":2,\"start\":4,\"length\":6,\"type\":\"assignment\"}],"
    "\"sourceTokens\":[{\"line\":0,\"start\":0,\"length\":5,\"type\":\"keyword\"},{"
    "\"line\":0,\"start\":6,\"length\":4,\"type\":\"reference\"},{\"line\":1,\"start\":0,"
    "\"length\":2,\"type\":\"keyword\"},{\"line\":1,\"start\":3,\"length\":5,\"type\":"
    "\"input\"},{\"line\":2,\"start\":0,\"length\":3,\"type\":\"keyword\"},{\"line\":2,"
    "\"start\":4,\"length\":6,\"type\":\"assignment\"},{\"line\":2,\"start\":11,\"length\":1,"
    "\"type\":\"assignment\"},{\"line\":2,\"start\":13,\"length\":5,\"type\":\"input\"}],"
    "\"functions\":[],\"diagnostics\":[]}\n";

static void test_outline_splits_comma_separated_outputs(void) {
    const char* source =
        "model demo\n"
        "entry = true\n"
        "exit = false\n"
        "out entry, exit\n";
    char* json = cxpr_document_analyze_json(source, "outputs.cxpr");

    assert(json != NULL);
    assert(strstr(json, "\"kind\":\"output\",\"name\":\"entry\",\"value\":\"\","
                        "\"startLine\":3,\"startChar\":4,\"endLine\":3,\"endChar\":9") != NULL);
    assert(strstr(json, "\"kind\":\"output\",\"name\":\"exit\",\"value\":\"\","
                        "\"startLine\":3,\"startChar\":11,\"endLine\":3,\"endChar\":15") != NULL);
    assert(strstr(json, "\"name\":\"entry, exit\"") == NULL);
    cxpr_document_json_free(json);
    printf("  ✓ test_outline_splits_comma_separated_outputs\n");
}

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

static void test_analysis_exposes_model_type(void) {
    const char* source =
        "model bollinger {\n"
        "  type = \"indicator\"\n"
        "}\n"
        "out value = 1\n";
    char* json = cxpr_document_analyze_json(source, "bollinger.cxpr");
    assert(json != NULL);
    assert(strstr(json, "\"modelType\":\"indicator\"") != NULL);
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

static void test_model_hover_exposes_typed_input_and_output_contract(void) {
    const char* source =
        "model inferred_session\n"
        "in session\n"
        "out { value = session.open + session.bar.close, early = session.bar_index < 3 }\n";
    char* json = cxpr_document_resolve_json(
        source, "session-signal.cxpr", 0u, 8u);
    assert(json != NULL);
    assert(strstr(json,
                  "\"typedSignature\":\"inferred_session(session: struct) -> { value: number, early: bool }\"") != NULL);
    assert(strstr(json,
                  "\"name\":\"session.open\",\"type\":\"number\",\"provenance\":\"inferred\"") != NULL);
    assert(strstr(json,
                  "\"name\":\"session.bar.close\",\"type\":\"number\",\"provenance\":\"inferred\"") != NULL);
    assert(strstr(json,
                  "\"outputs\":[{\"name\":\"value\",\"type\":\"number\"},{\"name\":\"early\",\"type\":\"bool\"}]") != NULL);
    cxpr_document_json_free(json);
}

static void test_inferred_input_leaf_hover_exposes_concrete_type(void) {
    const char* source =
        "model inferred_session\n"
        "in session\n"
        "out { value = session.open + 1 }\n";
    char* json = cxpr_document_resolve_json(
        source, "session-leaf.cxpr", 2u, 23u);
    char* model_json = cxpr_document_resolve_json(
        source, "session-leaf.cxpr", 0u, 8u);
    assert(json != NULL);
    assert(model_json != NULL);
    assert(strstr(json, "\"name\":\"session.open\"") != NULL);
    assert(strstr(json, "\"kind\":\"input\"") != NULL);
    assert(strstr(json, "\"type\":\"number\"") != NULL);
    assert(strstr(json, "\"provenance\":\"inferred\"") != NULL);
    assert(strstr(model_json,
                  "\"typedSignature\":\"inferred_session(session: struct) -> { value: number }\"") != NULL);
    cxpr_document_json_free(json);
    cxpr_document_json_free(model_json);
}

static void test_inferred_input_leaf_hover_survives_unresolved_imports(void) {
    const char* source =
        "model epic_section(pipe = \"epic\")\n"
        "use horn_section from instruments.brass\n"
        "in epic\n"
        "voice = horn_section(age=epic.epic_age)\n"
        "out voice\n";
    char* json = cxpr_document_resolve_json(
        source, "epic_section.cxpr", 3u, 38u);
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"epic.epic_age\"") != NULL);
    assert(strstr(json, "\"kind\":\"input\"") != NULL);
    assert(strstr(json, "\"type\":\"unknown\"") != NULL);
    assert(strstr(json, "\"provenance\":\"inferred\"") != NULL);
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

static void test_record_field_resolution_stays_in_enclosing_record(void) {
    const char* source =
        "model demo\n"
        "$ltf = { curve_min_return_pct = 0.001 }\n"
        "other = { momentum_curve_accel = false }\n"
        "ltf = {\n"
        "  momentum_curve_accel = true,\n"
        "  buy_pressure = momentum_curve_accel\n"
        "}\n";
    char* json = cxpr_document_resolve_json(source, "record-field.cxpr", 5u, 19u);
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"ltf.momentum_curve_accel\"") != NULL);
    assert(strstr(json, "\"kind\":\"variable\"") != NULL);
    assert(strstr(json,
                  "\"startLine\":4,\"startChar\":2,\"endLine\":4,\"endChar\":22") != NULL);
    cxpr_document_json_free(json);
}

static void test_record_field_resolution_includes_full_parent_path(void) {
    const char* source =
        "model demo\n"
        "clap = {\n"
        "  pattern = true,\n"
        "  nested = { enabled = pattern }\n"
        "}\n";
    char* pattern = cxpr_document_resolve_json(source, "record-path.cxpr", 2u, 4u);
    char* enabled = cxpr_document_resolve_json(source, "record-path.cxpr", 3u, 13u);
    assert(pattern != NULL);
    assert(enabled != NULL);
    assert(strstr(pattern, "\"name\":\"clap.pattern\"") != NULL);
    assert(strstr(enabled, "\"name\":\"clap.nested.enabled\"") != NULL);
    cxpr_document_json_free(pattern);
    cxpr_document_json_free(enabled);
}

static void test_record_object_field_resolution_includes_parent_path(void) {
    const char* source =
        "model demo\n"
        "toms = {\n"
        "  shape = { width = 10 }\n"
        "}\n";
    char* json = cxpr_document_resolve_json(source, "record-object-path.cxpr", 2u, 4u);
    assert(json != NULL);
    assert(strstr(json, "\"name\":\"toms.shape\"") != NULL);
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

static void test_multiline_call_named_arguments_are_not_assignments(void) {
    const char* source =
        "hall = reverb(source_left=0.34 * dry_left,\n"
        "    sample_rate=$sample_rate, decay_seconds=5.2)\n";
    char* json = cxpr_document_analyze_json(source, "multiline-call.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":4,\"length\":11,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":15,\"length\":1,\"type\":\"namedArgument\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_lambda_block_bindings_are_assignments_inside_call(void) {
    const char* source =
        "out voices = map(streams, stream => {\n"
        "    cursor = upper_bound(stream.at, t) - 1;\n"
        "    index = clamp(cursor, 0, length(stream.at) - 1);\n"
        "    return timed_voice(\n"
        "        t=t, cursor=cursor);\n"
        "})\n";
    char* json = cxpr_document_analyze_json(source, "lambda-bindings.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":4,\"length\":6,\"type\":\"assignment\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":4,\"length\":5,\"type\":\"assignment\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":8,\"length\":1,\"type\":\"namedArgument\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_function_declaration_parameters_are_semantic_parameters(void) {
    const char* source =
        "fn hz(note) = 440 * pow(2, (note - 69) / 12)\n"
        "fn blend(left, right) = left + right\n";
    char* json = cxpr_document_analyze_json(source, "function-parameters.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":6,\"length\":4,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":9,\"length\":4,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":15,\"length\":5,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":28,\"length\":4,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":24,\"length\":4,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":31,\"length\":5,\"type\":\"parameter\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_lambda_parameters_and_arrows_have_semantic_colors(void) {
    const char* source =
        "first = map(values, voice => fn(voice))\n"
        "second = map(values, (voice) => fn(voice))\n";
    char* json = cxpr_document_analyze_json(source, "lambda-colors.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":20,\"length\":5,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":26,\"length\":2,\"type\":\"operator\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":22,\"length\":5,\"type\":\"parameter\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":29,\"length\":2,\"type\":\"operator\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_declared_symbol_references_keep_semantic_colors(void) {
    const char* source =
        "value = 1\n"
        "state previous = 0\n"
        "previous := value\n"
        "out result = previous + value\n";
    char* json = cxpr_document_analyze_json(source, "symbol-colors.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":0,\"length\":8,\"type\":\"state\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":2,\"start\":12,\"length\":5,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":13,\"length\":8,\"type\":\"state\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":24,\"length\":5,\"type\":\"reference\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_record_and_pipe_binding_uses_are_references(void) {
    const char* source =
        "dry = sum([arpeggio, melody])\n"
        "early = dry |> gain(level=0.28)\n"
        "atmosphere = sum([dry, early])\n"
        "glue_source = sum([dry, early, atmosphere])\n"
        "stereo = glue_source\n"
        "out stereo\n";
    char* json = cxpr_document_analyze_json(source, "ambience-colors.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":8,\"length\":3,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":19,\"length\":3,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":24,\"length\":5,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":31,\"length\":10,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":0,\"length\":11,\"type\":\"assignment\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":4,\"start\":9,\"length\":11,\"type\":\"reference\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":5,\"start\":4,\"length\":6,\"type\":\"reference\"}") != NULL);
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
                  "{\"line\":0,\"start\":6,\"length\":19,\"type\":\"model\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":4,\"length\":4,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":1,\"start\":9,\"length\":1,\"type\":\"namedArgument\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":26,\"length\":8,\"type\":\"property\"}") == NULL);
    cxpr_document_json_free(json);

    source =
        "model bkadx_breakaway_ltf_base(timeframe = \"30m\",type = \"strategy\")\n";
    json = cxpr_document_analyze_json(source, "model-header.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "\"kind\":\"model\",\"name\":\"bkadx_breakaway_ltf_base\"") != NULL);
    assert(strstr(json, "\"name\":\"bkadx_breakaway_ltf_base(timeframe") == NULL);
    assert(strstr(json,
                  "{\"line\":0,\"start\":6,\"length\":24,\"type\":\"model\"}") != NULL);
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
    const char* source = "use indicators.bollinger\n";
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

static void test_unknown_symbol_location_ignores_comments(void) {
    const char* source =
        "model demo\n"
        "// A peer read such as n.dist uses the previous snapshot.\n"
        "value = n.dist\n";
    char* json = cxpr_document_analyze_json(source, "diagnostic-comment.cxpr");
    assert(json != NULL);
    assert(strstr(json, "Expression references unknown symbol 'n.dist'") != NULL);
    assert(strstr(json, "\"line\":2,\"column\":8") != NULL);
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

static void test_window_binding_hover_preserves_source_syntax(void) {
    const char* source =
        "model demo\n"
        "in source\n"
        "$period = 20\n"
        "middle = mean(window(source, $period))\n";
    char* json = cxpr_document_resolve_json(
        source, "window-binding-hover.cxpr", 3u, 2u);
    assert(json != NULL);
    assert(strstr(json,
                  "\"signature\":\"middle = mean(window(source, $period))\"") != NULL);
    assert(strstr(json, "__cxpr_window_mean") == NULL);
    cxpr_document_json_free(json);
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

static void test_map_is_valid_tooling_function(void) {
    const char* source =
        "model demo\n"
        "voices = map({ main = { gain = 0.8 } }, voice => voice.gain)\n"
        "out voices\n";
    char* analysis = cxpr_document_analyze_json(source, "map.cxpr");
    char* hover = cxpr_document_resolve_json(source, "map.cxpr", 1u, 10u);
    assert(analysis != NULL);
    assert(hover != NULL);
    assert(strstr(analysis, "unknown function 'map'") == NULL);
    assert(strstr(hover, "\"signature\":\"map(values, mapper)\"") != NULL);
    assert(strstr(hover, "\"builtin\":true") != NULL);
    cxpr_document_json_free(analysis);
    cxpr_document_json_free(hover);
}

static void test_named_map_binding_hover_lists_outputs(void) {
    const char* source =
        "model voices_demo\n"
        "voices = map({\n"
        "  main = { frequency = 440, gain = 0.8 },\n"
        "  back = { frequency = 550, gain = 0.5 },\n"
        "  third = { frequency = 660, gain = 0.4 }\n"
        "}, voice => oscillator(frequency=voice.frequency, gain=voice.gain))\n"
        "out sample = voices.main + voices.back + voices.third\n";
    char* hover = cxpr_document_resolve_json(source, "voices.cxpr", 6u, 15u);
    assert(hover != NULL);
    assert(strstr(hover,
                  "\"signature\":\"voices = { main, back, third }\"") != NULL);
    assert(strstr(hover, "frequency = 440") == NULL);
    assert(strstr(hover, "gain = 0.8") == NULL);
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
    assert(strstr(json, "\"imports\":[\"ts.pivots\",\"ts.window\"]") != NULL);
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

static void test_rooted_input_aliases_are_source_input_tokens(void) {
    const char* source =
        "in stereo { left as source_left, right as source_right }\n"
        "out mono = source_left + source_right\n";
    char* json = cxpr_document_analyze_json(source, "rooted-alias-tokens.cxpr");
    static const char* expected[] = {
        "{\"line\":0,\"start\":12,\"length\":4,\"type\":\"input\"}",
        "{\"line\":0,\"start\":20,\"length\":11,\"type\":\"input\"}",
        "{\"line\":0,\"start\":33,\"length\":5,\"type\":\"input\"}",
        "{\"line\":0,\"start\":42,\"length\":12,\"type\":\"input\"}",
        "{\"line\":1,\"start\":11,\"length\":11,\"type\":\"input\"}",
        "{\"line\":1,\"start\":25,\"length\":12,\"type\":\"input\"}"
    };
    assert(json != NULL);
    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        assert(strstr(json, expected[i]) != NULL);
    }
    cxpr_document_json_free(json);
}

static void test_struct_input_producer_is_typed_in_outline(void) {
    const char* source = "in position { entry_price, bars_in_position }\n";
    char* json = cxpr_document_analyze_json(source, "struct-input.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "\"kind\":\"input\",\"name\":\"position\",\"value\":\"struct\"") != NULL);
    assert(strstr(json,
                  "\"kind\":\"input\",\"name\":\"entry_price\",\"value\":\"\"") != NULL);
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

static void test_function_body_outline_exposes_native_expressions(void) {
    const char* source =
        "model demo\n"
        "fn risk(value) {\n"
        "  floor = max(0 - value, 0)\n"
        "  return floor\n"
        "}\n";
    char* json = cxpr_document_analyze_json(source, "function-flow.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "\"kind\":\"local\",\"name\":\"floor\",\"value\":\"\","
                  "\"expression\":\"max(0 - value, 0)\"") != NULL);
    assert(strstr(json,
                  "\"kind\":\"return\",\"name\":\"floor\",\"value\":\"\","
                  "\"expression\":\"floor\"") != NULL);
    cxpr_document_json_free(json);
}

static void test_function_parameters_preserve_origin_token_types(void) {
    const char* source =
        "model demo\n"
        "in close, high\n"
        "in session { open, bar_index }\n"
        "fn risk(close, high, session, bars) { return close + high }\n";
    char* json = cxpr_document_analyze_json(source, "function-params.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":3,\"length\":4,\"type\":\"function\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":8,\"length\":5,\"type\":\"input\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":15,\"length\":4,\"type\":\"input\"}") != NULL);
    assert(strstr(json,
                  "{\"line\":3,\"start\":21,\"length\":7,\"type\":\"reference\"}") != NULL);
    cxpr_document_json_free(json);
}

static void test_function_catalog_exposes_inferred_parameter_types(void) {
    const char* source =
        "model demo\n"
        "in close\n"
        "fn risk(close, peak_bars) {\n"
        "  peak = max(window(close, peak_bars))\n"
        "  return peak\n"
        "}\n";
    char* json = cxpr_document_analyze_json(source, "function-types.cxpr");
    assert(json != NULL);
    assert(strstr(json,
                  "\"name\":\"risk\",\"builtin\":false,\"minArgs\":2,\"maxArgs\":2") != NULL);
    assert(strstr(json,
                  "\"parameters\":[{\"name\":\"close\",\"type\":\"number\","
                  "\"role\":\"input\"},{\"name\":\"peak_bars\",\"type\":\"number\","
                  "\"role\":\"argument\"}]") != NULL);
    cxpr_document_json_free(json);
}


int main(void) {
    test_analysis_baseline();
    test_outline_splits_comma_separated_outputs();
    test_analysis_exposes_model_type();
    test_registry_signature_fields();
    test_marker_resolution();
    test_model_resolution_has_callable_metadata();
    test_model_hover_exposes_typed_input_and_output_contract();
    test_inferred_input_leaf_hover_exposes_concrete_type();
    test_inferred_input_leaf_hover_survives_unresolved_imports();
    test_binding_resolution_includes_expression();
    test_record_field_resolution_stays_in_enclosing_record();
    test_record_field_resolution_includes_full_parent_path();
    test_record_object_field_resolution_includes_parent_path();
    test_parameter_resolution_preserves_dollar_prefix();
    test_marker_completion();
    test_registry_catalog();
    test_host_builtin_catalog_merge();
    test_source_tokens_and_completion_contract();
    test_multiline_call_named_arguments_are_not_assignments();
    test_lambda_block_bindings_are_assignments_inside_call();
    test_function_declaration_parameters_are_semantic_parameters();
    test_lambda_parameters_and_arrows_have_semantic_colors();
    test_declared_symbol_references_keep_semantic_colors();
    test_record_and_pipe_binding_uses_are_references();
    test_model_name_semantic_token();
    test_output_metadata_is_named_argument();
    test_import_tokens_are_individual();
    test_direct_import_path_is_one_token();
    test_exported_and_internal_parameter_tokens();
    test_unknown_symbol_diagnostic_location();
    test_unknown_symbol_location_ignores_identifier_prefixes();
    test_unknown_symbol_location_ignores_comments();
    test_runtime_symbol_in_parameter_reports_source_location();
    test_window_syntax_hover_metadata();
    test_window_binding_hover_preserves_source_syntax();
    test_variadic_max_hover_metadata();
    test_cross_below_hover_metadata();
    test_stacked_builtin_and_anchor_token_boundary();
    test_resample_is_valid_tooling_function();
    test_map_is_valid_tooling_function();
    test_named_map_binding_hover_lists_outputs();
    test_metadata_min_max_are_not_builtin_functions();
    test_unresolved_resolution_exposes_c_parsed_imports();
    test_operator_token_does_not_match_identifier_substring();
    test_multiline_boolean_operators_are_keywords();
    test_comma_separated_inputs_are_source_input_tokens();
    test_rooted_input_aliases_are_source_input_tokens();
    test_struct_input_producer_is_typed_in_outline();
    test_optimize_block_is_named_argument();
    test_function_body_outline_exposes_native_expressions();
    test_function_parameters_preserve_origin_token_types();
    test_function_catalog_exposes_inferred_parameter_types();
    puts("doc tooling tests passed");
    return 0;
}
