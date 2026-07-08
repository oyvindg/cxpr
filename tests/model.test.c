/**
 * @file model.test.c
 * @brief Tests for the host-agnostic .cxpr model parser.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CXPR_TEST_WITH_CXTA
#include <cxta/cxta.h>
#endif

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* f;
    long size;
    char* data;

    snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open fixture: %s\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    fclose(f);
    return data;
}

static char* join_sources(const char* first, const char* second) {
    size_t first_len;
    size_t second_len;
    char* joined;
    assert(first != NULL);
    assert(second != NULL);
    first_len = strlen(first);
    second_len = strlen(second);
    joined = (char*)malloc(first_len + second_len + 2u);
    assert(joined != NULL);
    memcpy(joined, first, first_len);
    joined[first_len] = '\n';
    memcpy(joined + first_len + 1u, second, second_len);
    joined[first_len + 1u + second_len] = '\0';
    return joined;
}

static cxpr_model* parse_model_ok(const char* source) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    if (!model) {
        fprintf(stderr, "parse model failed: %s at %zu:%zu\n",
                err.message ? err.message : "(null)", err.line, err.column);
    }
    assert(model != NULL);
    assert(err.code == CXPR_OK);
    return model;
}

static const cxpr_provider_scope_spec model_timeframe_scope = {"timeframe", true};
static const cxpr_provider_source_spec model_close_source = {
    "close", 0u, 1u, &model_timeframe_scope};
static const cxpr_provider_fn_spec* const model_functions[] = {NULL};
static const cxpr_provider_source_spec* const model_sources[] = {&model_close_source};

static const cxpr_provider_fn_spec* const*
model_fn_specs(const void* userdata, size_t* count) {
    (void)userdata;
    if (count) *count = 0u;
    return model_functions;
}

static const cxpr_provider_fn_spec*
model_fn_spec_find(const void* userdata, const char* name) {
    (void)userdata;
    (void)name;
    return NULL;
}

static const cxpr_provider_source_spec* const*
model_source_specs(const void* userdata, size_t* count) {
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(model_sources);
    return model_sources;
}

static const cxpr_provider_source_spec*
model_source_spec_find(const void* userdata, const char* name) {
    (void)userdata;
    if (name && strcmp(name, "close") == 0) return &model_close_source;
    return NULL;
}

static const cxpr_provider_vtable model_provider_vtable = {
    .fn_specs = model_fn_specs,
    .fn_spec_find = model_fn_spec_find,
    .source_specs = model_source_specs,
    .source_spec_find = model_source_spec_find,
    .expr_param_spec_for = NULL,
};

static const cxpr_provider model_provider = {
    .name = "model_provider",
    .userdata = NULL,
    .vtable = &model_provider_vtable,
};

typedef struct {
    size_t bind_count;
    char names[4][32];
    char scopes[4][32];
} model_source_capture;

static int model_source_bind(const cxpr_source_plan_node* node,
                             const double* bound_args,
                             size_t arg_count,
                             uint64_t* out_handle,
                             void* userdata) {
    model_source_capture* capture = (model_source_capture*)userdata;
    size_t index;

    (void)bound_args;
    assert(capture != NULL);
    assert(node != NULL);
    assert(out_handle != NULL);
    assert(arg_count == 0u);
    assert(capture->bind_count < CXPR_ARRAY_COUNT(capture->names));

    index = capture->bind_count++;
    snprintf(capture->names[index], sizeof(capture->names[index]), "%s",
             node->name ? node->name : "");
    snprintf(capture->scopes[index], sizeof(capture->scopes[index]), "%s",
             node->scope_value ? node->scope_value : "");

    if (strcmp(node->name, "close") == 0 &&
        node->scope_value &&
        strcmp(node->scope_value, "1d") == 0) {
        *out_handle = 1001u;
        return 1;
    }
    if (strcmp(node->name, "close") == 0 &&
        node->scope_value &&
        strcmp(node->scope_value, "1h") == 0) {
        *out_handle = 1002u;
        return 1;
    }
    return 0;
}

static int model_source_resolve(uint64_t handle,
                                const char* source_name,
                                double* out_value,
                                void* userdata) {
    (void)userdata;
    if (!source_name || strcmp(source_name, "close") != 0 || !out_value) return 0;
    if (handle == 1001u) {
        *out_value = 10.0;
        return 1;
    }
    if (handle == 1002u) {
        *out_value = 11.0;
        return 1;
    }
    return 0;
}

static void test_parse_minimal_strategy_model(void) {
    const char* source =
        "name macd\n"
        "use indicators\n"
        "in { close, position }\n"
        "$fast = 12\n"
        "$slow = 26\n"
        "fast = ema(close, period = $fast)\n"
        "slow = ema(close, period = $slow)\n"
        "buy = cross_above(fast, slow) and not position\n"
        "out buy\n";

    cxpr_model* model = parse_model_ok(source);

    assert(strcmp(cxpr_model_name(model), "macd") == 0);
    assert(cxpr_model_use_count(model) == 1);
    assert(strcmp(cxpr_model_use(model, 0), "indicators") == 0);
    assert(cxpr_model_input_count(model) == 2);
    assert(strcmp(cxpr_model_input(model, 0), "close") == 0);
    assert(strcmp(cxpr_model_input(model, 1), "position") == 0);
    assert(cxpr_model_constant_count(model) == 2);
    assert(strcmp(cxpr_model_constant_name(model, 0), "fast") == 0);
    assert(cxpr_ast_type(cxpr_model_constant_expr(model, 0)) == CXPR_NODE_NUMBER);
    assert(cxpr_model_binding_count(model) == 3);
    assert(strcmp(cxpr_model_binding_name(model, 2), "buy") == 0);
    assert(cxpr_model_binding_kind_at(model, 2) == CXPR_MODEL_BINDING_EXPR);
    assert(cxpr_ast_type(cxpr_model_binding_expr(model, 2)) == CXPR_NODE_BINARY_OP);
    assert(cxpr_model_output_count(model) == 1);
    assert(strcmp(cxpr_model_output(model, 0), "buy") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_parse_minimal_strategy_model\n");
}

static void test_parse_model_with_declaration_metadata_blocks(void) {
    const char* source =
        "name decorated_model {\n"
        "    id = \"decorated_model\"\n"
        "    hover = \"First line\n"
        "    second line\"\n"
        "}\n"
        "use ema\n"
        "in close\n"
        "$period = 20 { default = 20 }\n"
        "out close {\n"
        "        label = \"Value\"\n"
        "        plot { color = \"#22c55e\" }\n"
        "}\n"
        "\n";

    cxpr_model* model = parse_model_ok(source);

    assert(strcmp(cxpr_model_name(model), "decorated_model") == 0);
    assert(cxpr_model_use_count(model) == 1);
    assert(strcmp(cxpr_model_use(model, 0), "ema") == 0);
    assert(cxpr_model_input_count(model) == 1);
    assert(strcmp(cxpr_model_input(model, 0), "close") == 0);
    assert(cxpr_model_output_count(model) == 1);
    assert(strcmp(cxpr_model_output(model, 0), "close") == 0);
    assert(cxpr_model_metadata_count(model) == 3);
    assert(strcmp(cxpr_model_metadata_name(model, 0), "model") == 0);
    assert(cxpr_model_metadata_target_kind_at(model, 0) ==
           CXPR_MODEL_METADATA_TARGET_MODEL);
    assert(strcmp(cxpr_model_metadata_target_name(model, 0), "decorated_model") == 0);
    assert(strstr(cxpr_model_metadata_body(model, 0), "second line") != NULL);
    assert(strcmp(cxpr_model_metadata_name(model, 1), "param") == 0);
    assert(cxpr_model_metadata_target_kind_at(model, 1) ==
           CXPR_MODEL_METADATA_TARGET_PARAM);
    assert(strcmp(cxpr_model_metadata_target_name(model, 1), "period") == 0);
    assert(strcmp(cxpr_model_metadata_name(model, 2), "output") == 0);
    assert(cxpr_model_metadata_target_kind_at(model, 2) ==
           CXPR_MODEL_METADATA_TARGET_OUTPUT);
    assert(strcmp(cxpr_model_metadata_target_name(model, 2), "close") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_parse_model_with_declaration_metadata_blocks\n");
}

static void test_parse_model_rejects_legacy_meta_block(void) {
    const char* source =
        "meta {\n"
        "    kind = \"indicator\"\n"
        "}\n"
        "name legacy_meta\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    assert(model == NULL);
    assert(err.code != CXPR_OK);
    printf("  ✓ test_parse_model_rejects_legacy_meta_block\n");
}

static void test_parse_model_rejects_decorator_metadata(void) {
    const char* source =
        "@plot {\n"
        "    color = \"#22c55e\"\n"
        "}\n"
        "name decorated\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    assert(model == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strstr(err.message, "Decorator metadata is not supported") != NULL);
    printf("  ✓ test_parse_model_rejects_decorator_metadata\n");
}

static void test_parse_model_host_blocks_preserve_body(void) {
    const char* source =
        "book market_neutral {\n"
        "    symbols {\n"
        "        primary {\n"
        "            symbol = \"TSLA\"\n"
        "            strategy = \"market_neutral\"\n"
        "        }\n"
        "    }\n"
        "    hedging = [\"beta_hedge\"]\n"
        "}\n"
        "\n"
        "session market_neutral_backtest {\n"
        "    mode = backtest\n"
        "    source = csv\n"
        "    books = [\"market_neutral\"]\n"
        "    broker = \"simulated\"\n"
        "}\n"
        "name host_config_bundle\n";
    cxpr_model* model = parse_model_ok(source);
    const char* body;

    assert(strcmp(cxpr_model_name(model), "host_config_bundle") == 0);
    assert(cxpr_model_host_block_count(model) == 2u);
    assert(strcmp(cxpr_model_host_block_kind(model, 0), "book") == 0);
    assert(strcmp(cxpr_model_host_block_name(model, 0), "market_neutral") == 0);
    body = cxpr_model_host_block_body(model, 0);
    assert(body != NULL);
    assert(strstr(body, "symbols {\n") != NULL);
    assert(strstr(body, "        primary {\n") != NULL);
    assert(strstr(body, "            symbol = \"TSLA\"\n") != NULL);
    assert(strstr(body, "hedging = [\"beta_hedge\"]") != NULL);

    assert(strcmp(cxpr_model_host_block_kind(model, 1), "session") == 0);
    assert(strcmp(cxpr_model_host_block_name(model, 1), "market_neutral_backtest") == 0);
    body = cxpr_model_host_block_body(model, 1);
    assert(body != NULL);
    assert(strstr(body, "books = [\"market_neutral\"]\n") != NULL);
    assert(strstr(body, "broker = \"simulated\"") != NULL);

    cxpr_model_free(model);
    printf("  ✓ test_parse_model_host_blocks_preserve_body\n");
}

static void test_parse_model_rejects_yaml_mapping_in_host_block(void) {
    const char* source =
        "book market_neutral {\n"
        "symbols:\n"
        "  primary:\n"
        "    symbol: TSLA\n"
        "}\n"
        "name invalid_yaml_host\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    assert(model == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strstr(err.message, "Host block body must use cxpr syntax") != NULL);
    printf("  ✓ test_parse_model_rejects_yaml_mapping_in_host_block\n");
}

static void test_parse_model_rejects_meta_as_host_block(void) {
    const char* source =
        "meta legacy {\n"
        "    kind = \"indicator\"\n"
        "}\n"
        "name invalid_meta_host\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    assert(model == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strstr(err.message, "Legacy meta blocks are not supported") != NULL);
    printf("  ✓ test_parse_model_rejects_meta_as_host_block\n");
}

static void test_model_plan_bind_sources_exports_scoped_timeframes(void) {
    const char* source =
        "name scoped_market\n"
        "daily = close(timeframe=\"1d\")\n"
        "hourly = close(\"1h\")\n"
        "out signal = hourly > daily\n";
    cxpr_model* model = parse_model_ok(source);
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_source_plan_bindings bindings = {0};
    model_source_capture capture = {0};
    cxpr_plan_config config = {
        .bind = model_source_bind,
        .resolve = model_source_resolve,
        .userdata = &capture,
    };
    cxpr_error err = {0};
    size_t min_args = 0u;
    size_t max_args = 0u;

    assert(reg != NULL);
    assert(ctx != NULL);

    assert(cxpr_model_plan_bind_sources(
        model, &model_provider, ctx, reg, &config, &bindings, &err));
    assert(err.code == CXPR_OK);
    assert(capture.bind_count == 2u);
    assert(bindings.count == 2u);
    assert(bindings.handles[0] == 1001u);
    assert(bindings.handles[1] == 1002u);
    assert(strcmp(capture.names[0], "close") == 0);
    assert(strcmp(capture.scopes[0], "1d") == 0);
    assert(strcmp(capture.names[1], "close") == 0);
    assert(strcmp(capture.scopes[1], "1h") == 0);
    assert(cxpr_registry_lookup(reg, "close", &min_args, &max_args));
    assert(min_args == 0u);
    assert(max_args == 1u);

    cxpr_free_source_plan_bindings(&bindings);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_model_free(model);
    printf("  ✓ test_model_plan_bind_sources_exports_scoped_timeframes\n");
}

static void test_parse_state_init_and_update_state(void) {
    const char* source =
        "name integrator\n"
        "in { dr, dt }\n"
        "state {\n"
        "    r = 100\n"
        "}\n"
        "update state {\n"
        "    r = r + dr * dt\n"
        "}\n"
        "out r\n";

    cxpr_model* model = parse_model_ok(source);

    assert(cxpr_model_binding_count(model) == 2);
    assert(strcmp(cxpr_model_binding_name(model, 0), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 0) == CXPR_MODEL_BINDING_STATE);
    assert(strcmp(cxpr_model_binding_name(model, 1), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 1) == CXPR_MODEL_BINDING_STATE_UPDATE);
    assert(cxpr_ast_type(cxpr_model_binding_expr(model, 1)) == CXPR_NODE_BINARY_OP);

    cxpr_model_free(model);
    printf("  ✓ test_parse_state_init_and_update_state\n");
}

static void test_reject_legacy_state_assignment_syntax(void) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(
        "name invalid_legacy_state\n"
        "state r = 100\n",
        &err);

    assert(model == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "State declarations must use state block") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_reject_legacy_state_assignment_syntax\n");
}

static void test_parse_out_expr_assignment_adds_output(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name out_expr_assignment\n"
        "in { close }\n"
        "out signal = close > 10\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_binding_count(model) == 1);
    assert(strcmp(cxpr_model_binding_name(model, 0), "signal") == 0);
    assert(cxpr_model_binding_kind_at(model, 0) == CXPR_MODEL_BINDING_EXPR);
    assert(cxpr_model_output_count(model) == 1);
    assert(strcmp(cxpr_model_output(model, 0), "signal") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_parse_out_expr_assignment_adds_output\n");
}

static void test_parse_update_state_does_not_add_output(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name hidden_state_update\n"
        "state {\n"
        "    r = 0\n"
        "}\n"
        "update state {\n"
        "    r = r + 1\n"
        "}\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_binding_count(model) == 2);
    assert(strcmp(cxpr_model_binding_name(model, 1), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 1) == CXPR_MODEL_BINDING_STATE_UPDATE);
    assert(cxpr_model_output_count(model) == 0);

    cxpr_model_free(model);
    printf("  ✓ test_parse_update_state_does_not_add_output\n");
}

static void test_parse_update_state_with_explicit_output(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name public_state_update\n"
        "state {\n"
        "    r = 0\n"
        "}\n"
        "update state {\n"
        "    r = r + 1\n"
        "}\n"
        "out r\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_binding_count(model) == 2);
    assert(strcmp(cxpr_model_binding_name(model, 1), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 1) == CXPR_MODEL_BINDING_STATE_UPDATE);
    assert(cxpr_model_output_count(model) == 1);
    assert(strcmp(cxpr_model_output(model, 0), "r") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_parse_update_state_with_explicit_output\n");
}

static void test_update_state_supports_block_local_temporaries(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name update_locals\n"
        "in { close }\n"
        "state {\n"
        "    r = 0\n"
        "}\n"
        "update state {\n"
        "    next =\n"
        "        close + 1\n"
        "    doubled = next * 2\n"
        "    r = doubled\n"
        "}\n"
        "out r\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_binding_count(model) == 4);
    assert(strcmp(cxpr_model_binding_name(model, 0), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 0) == CXPR_MODEL_BINDING_STATE);
    assert(strcmp(cxpr_model_binding_name(model, 1), "next") == 0);
    assert(cxpr_model_binding_kind_at(model, 1) == CXPR_MODEL_BINDING_LOCAL);
    assert(strcmp(cxpr_model_binding_name(model, 2), "doubled") == 0);
    assert(cxpr_model_binding_kind_at(model, 2) == CXPR_MODEL_BINDING_LOCAL);
    assert(strcmp(cxpr_model_binding_name(model, 3), "r") == 0);
    assert(cxpr_model_binding_kind_at(model, 3) == CXPR_MODEL_BINDING_STATE_UPDATE);
    {
        cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
        char* code = NULL;
        assert(program != NULL);
        code = cxpr_model_program_to_c_tick_function(program, "static inline",
                                                     "update_locals_tick", &err);
        assert(code != NULL);
        assert(strstr(code, "const double next ="));
        assert(strstr(code, "const double doubled ="));
        assert(strstr(code, "_cx_next_r = doubled"));
        free(code);
        cxpr_model_program_free(program);
    }

    cxpr_model_free(model);
    printf("  ✓ test_update_state_supports_block_local_temporaries\n");
}

static void test_reject_legacy_out_state_update_syntax(void) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(
        "name invalid_legacy_update\n"
        "state {\n"
        "    r = 0\n"
        "}\n"
        "out r = r + 1\n",
        &err);

    assert(model == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "State updates must use update state block") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_reject_legacy_out_state_update_syntax\n");
}

static void test_validate_rejects_duplicate_state_declaration(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name invalid_duplicate_state\n"
        "state {\n"
        "    r = 100\n"
        "    r = r + 1\n"
        "}\n"
        "out r\n");

    assert(!cxpr_model_validate(model, &err));
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "Duplicate binding") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_validate_rejects_duplicate_state_declaration\n");
}

static void test_reject_invalid_out_assignment_name(void) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model("name bad\nout 123 = close\n", &err);
    assert(model == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strcmp(err.message, "Invalid output name") == 0);
    cxpr_model_free(model);
    printf("  ✓ test_reject_invalid_out_assignment_name\n");
}

static void test_reject_invalid_output_name(void) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model("name bad\nout 123\n", &err);
    assert(model == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strcmp(err.message, "Invalid output name") == 0);
    cxpr_model_free(model);
    printf("  ✓ test_reject_invalid_output_name\n");
}

static void test_validate_accepts_known_symbols(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name valid\n"
        "in { close }\n"
        "$period = 20\n"
        "avg = ema(close, $period)\n"
        "signal = close > avg\n"
        "out signal\n");

    assert(cxpr_model_validate(model, &err));
    assert(err.code == CXPR_OK);

    cxpr_model_free(model);
    printf("  ✓ test_validate_accepts_known_symbols\n");
}

static void test_validate_rejects_unknown_output(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name invalid_output\n"
        "in { close }\n"
        "signal = close > 0\n"
        "out missing\n");

    assert(!cxpr_model_validate(model, &err));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Output references unknown symbol") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_validate_rejects_unknown_output\n");
}

static void test_validate_rejects_unknown_references(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name invalid_ref\n"
        "in { close }\n"
        "signal = close > missing\n"
        "out signal\n");

    assert(!cxpr_model_validate(model, &err));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Expression references unknown symbol") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_validate_rejects_unknown_references\n");
}

static void test_validate_rejects_unknown_constants(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name invalid_param\n"
        "in { close }\n"
        "signal = close > $threshold\n"
        "out signal\n");

    assert(!cxpr_model_validate(model, &err));
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    assert(strcmp(err.message, "Expression references unknown constant") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_validate_rejects_unknown_constants\n");
}

static void test_validate_rejects_duplicate_symbols(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name duplicate\n"
        "in { close, close }\n"
        "out close\n");

    assert(!cxpr_model_validate(model, &err));
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "Duplicate input") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_validate_rejects_duplicate_symbols\n");
}

static void test_eval_order_uses_existing_expression_toposort(void) {
    cxpr_error err = {0};
    size_t order[3] = {99, 99, 99};
    cxpr_model* model = parse_model_ok(
        "name ordered\n"
        "in { close }\n"
        "signal = fast > close\n"
        "fast = slow + 1\n"
        "slow = close * 2\n"
        "out signal\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_eval_order(model, order, 3, &err));
    assert(err.code == CXPR_OK);
    assert(strcmp(cxpr_model_binding_name(model, order[0]), "slow") == 0);
    assert(strcmp(cxpr_model_binding_name(model, order[1]), "fast") == 0);
    assert(strcmp(cxpr_model_binding_name(model, order[2]), "signal") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_eval_order_uses_existing_expression_toposort\n");
}

static void test_eval_order_rejects_cycles(void) {
    cxpr_error err = {0};
    size_t order[2] = {0, 0};
    cxpr_model* model = parse_model_ok(
        "name cycle\n"
        "a = b + 1\n"
        "b = a + 1\n"
        "out a\n");

    assert(cxpr_model_validate(model, &err));
    assert(!cxpr_model_eval_order(model, order, 2, &err));
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    cxpr_model_free(model);
    printf("  ✓ test_eval_order_rejects_cycles\n");
}

static void test_compile_and_eval_model_program(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name compiled\n"
        "in { close }\n"
        "$offset = 3\n"
        "base = close + $offset\n"
        "signal = base > 10\n"
        "out signal\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();

    assert(program != NULL);
    assert(ctx != NULL);
    assert(cxpr_model_program_binding_count(program) == 2);
    assert(strcmp(cxpr_model_program_binding_name(program, 0), "base") == 0);
    assert(strcmp(cxpr_model_program_binding_name(program, 1), "signal") == 0);
    assert(cxpr_model_program_output_count(program) == 1);
    assert(strcmp(cxpr_model_program_output_name(program, 0), "signal") == 0);

    assert(cxpr_model_program_seed_defaults(program, ctx, NULL, &err));
    assert(cxpr_context_get_param(ctx, "offset", &found) == 3.0);
    assert(found);

    cxpr_context_set(ctx, "close", 8.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "base", &found) == 11.0);
    assert(found);
    assert(cxpr_context_get_bool(ctx, "signal", &found));
    assert(found);

    cxpr_context_set_param(ctx, "offset", 1.0);
    cxpr_context_set(ctx, "close", 8.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "base", &found) == 9.0);
    assert(found);
    assert(!cxpr_context_get_bool(ctx, "signal", &found));
    assert(found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_compile_and_eval_model_program\n");
}

static void test_compile_model_defined_function_without_host_registration(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name fn_model\n"
        "in { close }\n"
        "fn above(src, threshold) = src > threshold\n"
        "signal = above(close, 10)\n"
        "out signal\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();
    char* fn_c;

    assert(program != NULL);
    assert(ctx != NULL);
    fn_c = cxpr_model_program_function_to_c_function(program, "above",
                                                     "static inline",
                                                     "double",
                                                     "cxpr_fn_above",
                                                     &err);
    assert(fn_c != NULL);
    assert(strstr(fn_c, "static inline double cxpr_fn_above(double src, double threshold)"));
    assert(!strstr(fn_c, "cxpr_registry"));
    free(fn_c);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get_bool(ctx, "signal", &found));
    assert(found);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(!cxpr_context_get_bool(ctx, "signal", &found));
    assert(found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_compile_model_defined_function_without_host_registration\n");
}

static void test_output_list_syntax(void) {
    cxpr_error err = {0};
    cxpr_model* model = parse_model_ok(
        "name outputs\n"
        "in { close }\n"
        "buy = close > 10\n"
        "sell = close < 5\n"
        "out { buy, sell }\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_output_count(model) == 2);
    assert(strcmp(cxpr_model_output(model, 0), "buy") == 0);
    assert(strcmp(cxpr_model_output(model, 1), "sell") == 0);

    cxpr_model_free(model);
    model = parse_model_ok(
        "name output_shorthand\n"
        "in close\n"
        "buy = close > 10\n"
        "sell = close < 5\n"
        "out buy, sell\n");

    assert(cxpr_model_validate(model, &err));
    assert(cxpr_model_output_count(model) == 2);
    assert(strcmp(cxpr_model_output(model, 0), "buy") == 0);
    assert(strcmp(cxpr_model_output(model, 1), "sell") == 0);

    cxpr_model_free(model);
    printf("  ✓ test_output_list_syntax\n");
}

static void test_function_block_out_expression(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name fn_block\n"
        "in { close }\n"
        "fn above(src, threshold) {\n"
        "    diff = src - threshold\n"
        "    magnitude = abs(diff)\n"
        "    return magnitude > 0 and diff > 0\n"
        "}\n"
        "signal = above(close, 10)\n"
        "out signal\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();

    assert(program != NULL);
    assert(ctx != NULL);
    assert(cxpr_model_program_function_count(program) == 2);
    cxpr_context_set(ctx, "close", 12.0);
    if (!cxpr_eval_model_program(program, ctx, NULL, &err)) {
        fprintf(stderr, "function block eval failed: %s\n", err.message);
        assert(0);
    }
    assert(cxpr_context_get_bool(ctx, "signal", &found));
    assert(found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_function_block_out_expression\n");
}

static void test_state_session_atomic_commit_and_events(void) {
    cxpr_error err = {0};
    bool found = false;
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name accumulator\n"
        "in { delta }\n"
        "$limit = 2\n"
        "state {\n"
        "    total = 0\n"
        "}\n"
        "next = total + delta\n"
        "update state {\n"
        "    total = next\n"
        "}\n"
        "hot = total >= $limit\n"
        "out { hot, total }\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "delta", 1.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "total", &found) == 1.0 && found);
    assert(cxpr_model_session_output_bool(session, "hot", &value));
    assert(!value);
    assert(!cxpr_model_session_output_rising(session, "hot"));
    assert(cxpr_model_session_output_changed(session, "hot"));

    cxpr_context_set(ctx, "delta", 1.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "total", &found) == 2.0 && found);
    assert(cxpr_model_session_output_bool(session, "hot", &value));
    assert(!value);
    assert(!cxpr_model_session_output_rising(session, "hot"));
    assert(!cxpr_model_session_output_changed(session, "hot"));

    cxpr_context_set(ctx, "delta", 0.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "total", &found) == 2.0 && found);
    assert(cxpr_model_session_output_bool(session, "hot", &value));
    assert(value);
    assert(cxpr_model_session_output_rising(session, "hot"));
    assert(cxpr_model_session_output_changed(session, "hot"));

    cxpr_context_set(ctx, "delta", -3.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "total", &found) == -1.0 && found);
    assert(cxpr_model_session_output_bool(session, "hot", &value));
    assert(value);
    assert(!cxpr_model_session_output_falling(session, "hot"));

    cxpr_context_set(ctx, "delta", 0.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "hot", &value));
    assert(!value);
    assert(cxpr_model_session_output_falling(session, "hot"));
    assert(cxpr_model_session_output_changed(session, "hot"));

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_state_session_atomic_commit_and_events\n");
}

static void test_session_input_lookback(void) {
    cxpr_error err = {0};
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name input_lookback\n"
        "in { close }\n"
        "up = close > close[1]\n"
        "out up\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "up", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "up", &value));
    assert(value);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "up", &value));
    assert(!value);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_session_input_lookback\n");
}

static void test_session_expression_lookback(void) {
    cxpr_error err = {0};
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name expression_lookback\n"
        "in { close }\n"
        "fast = close * 2\n"
        "crossed = fast > fast[1]\n"
        "out crossed\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "crossed", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "crossed", &value));
    assert(value);

    cxpr_context_set(ctx, "close", 10.5);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "crossed", &value));
    assert(!value);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_session_expression_lookback\n");
}

static void test_session_record_field_lookback(void) {
    cxpr_error err = {0};
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name record_field_lookback\n"
        "in { close }\n"
        "fn macd_like(src) {\n"
        "    line = src - 10\n"
        "    signal = 0\n"
        "    histogram = line - signal\n"
        "    return { line, signal, histogram }\n"
        "}\n"
        "m = macd_like(close)\n"
        "entry = m.histogram > 0 and m.histogram[1] <= 0\n"
        "out entry\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;

    if (!program) {
        fprintf(stderr, "record field lookback compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(value);

    cxpr_context_set(ctx, "close", 12.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_session_record_field_lookback\n");
}

static void test_session_direct_record_producer_lookback(void) {
    cxpr_error err = {0};
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name direct_record_producer_lookback\n"
        "in { close }\n"
        "fn macd_like(src) {\n"
        "    line = src - 10\n"
        "    signal = 0\n"
        "    histogram = line - signal\n"
        "    return { line, signal, histogram }\n"
        "}\n"
        "entry = macd_like(close).histogram > 0 and macd_like(close).histogram[1] <= 0\n"
        "out entry\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;
    char* code;

    if (!program) {
        fprintf(stderr, "direct record producer lookback compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    code = cxpr_model_program_to_c_tick_function(program, "static inline",
                                                 "direct_record_producer_lookback_tick", &err);
    if (!code) {
        fprintf(stderr, "direct record producer lookback C emit failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(code != NULL);
    assert(strstr(code, "const double entry ="));
    assert(strstr(code, "_cx_slots["));
    free(code);

    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(value);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_session_direct_record_producer_lookback\n");
}

static void test_macd_record_cross_strategy_fixture(void) {
    cxpr_error err = {0};
    bool value = false;
    char* source = read_fixture("fixtures/strategies/macd_record_cross_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(source != NULL);
    model = parse_model_ok(source);
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "macd record fixture compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);
    assert(cxpr_model_session_output_bool(session, "exit", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 11.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(value);

    cxpr_context_set(ctx, "close", 9.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_model_session_output_bool(session, "exit", &value));
    assert(value);

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    printf("  ✓ test_macd_record_cross_strategy_fixture\n");
}

static void test_robot_hexapod_fixture_simulates_vec3_io(void) {
    typedef struct {
        double gyro[3];
        double accel[3];
        double phase;
        double stride;
        double turn;
        double height;
        bool expected_stable;
    } frame_t;

    static const char* xyz[] = {"x", "y", "z"};
    const frame_t frames[] = {
        {{0.01, -0.02, 0.05}, {0.0, 0.0, 1.0}, 0.00, 0.55, 0.10, 0.18, true},
        {{0.02, -0.01, 0.08}, {0.1, 0.0, 0.995}, 0.25, 0.65, -0.05, 0.19, true},
        {{0.00, 0.00, 9.50}, {0.0, 0.0, 1.0}, 0.50, 0.40, 0.00, 0.18, false}
    };
    cxpr_error err = {0};
    bool bool_value = false;
    bool found = false;
    double first_coxa = 0.0;
    char* robotics_source = read_fixture("fixtures/robotics.cxpr");
    char* robot_source = read_fixture("fixtures/robot_hexapod.cxpr");
    char* source;
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(robotics_source != NULL);
    assert(robot_source != NULL);
    source = join_sources(robotics_source, robot_source);
    model = parse_model_ok(source);
    assert(cxpr_model_input_count(model) == 6);
    assert(cxpr_model_use_count(model) == 1);
    assert(strcmp(cxpr_model_use(model, 0), "robotics") == 0);
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "robot hexapod fixture compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    assert(cxpr_model_program_output_count(program) == 19);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    for (size_t i = 0; i < CXPR_ARRAY_COUNT(frames); ++i) {
        cxpr_value orientation_w;
        cxpr_value orientation_x;
        cxpr_value orientation_y;
        cxpr_value orientation_z;
        cxpr_value orientation_state;
        cxpr_value front_left_coxa;
        cxpr_value front_left_knee;
        cxpr_value front_left_ankle;
        cxpr_value front_left_joint_x;
        double q_norm;

        cxpr_context_set_fields(ctx, "gyro", xyz, frames[i].gyro, 3);
        cxpr_context_set_fields(ctx, "accel", xyz, frames[i].accel, 3);
        cxpr_context_set(ctx, "gait_phase", frames[i].phase);
        cxpr_context_set(ctx, "stride_command", frames[i].stride);
        cxpr_context_set(ctx, "turn_command", frames[i].turn);
        cxpr_context_set(ctx, "body_height", frames[i].height);

        if (!cxpr_model_session_tick(program, session, NULL, &err)) {
            fprintf(stderr, "robot hexapod fixture tick failed: %s\n",
                    err.message ? err.message : "(null)");
            assert(0);
        }

        assert(cxpr_model_session_output_bool(session, "stable", &bool_value));
        assert(bool_value == frames[i].expected_stable);
        orientation_w = cxpr_context_get_field(ctx, "orientation", "w", &found);
        assert(found && orientation_w.type == CXPR_VALUE_NUMBER);
        orientation_x = cxpr_context_get_field(ctx, "orientation", "x", &found);
        assert(found && orientation_x.type == CXPR_VALUE_NUMBER);
        orientation_y = cxpr_context_get_field(ctx, "orientation", "y", &found);
        assert(found && orientation_y.type == CXPR_VALUE_NUMBER);
        orientation_z = cxpr_context_get_field(ctx, "orientation", "z", &found);
        assert(found && orientation_z.type == CXPR_VALUE_NUMBER);
        q_norm = sqrt(orientation_w.d * orientation_w.d +
                      orientation_x.d * orientation_x.d +
                      orientation_y.d * orientation_y.d +
                      orientation_z.d * orientation_z.d);
        assert(fabs(q_norm - 1.0) < 1e-9);
        orientation_state = cxpr_context_get_typed(ctx, "orientation", &found);
        assert(found && orientation_state.type == CXPR_VALUE_STRUCT);

        front_left_coxa = cxpr_context_get_field(ctx, "front_left", "coxa", &found);
        assert(found && front_left_coxa.type == CXPR_VALUE_NUMBER);
        front_left_knee = cxpr_context_get_field(ctx, "front_left", "knee", &found);
        assert(found && front_left_knee.type == CXPR_VALUE_NUMBER);
        front_left_ankle = cxpr_context_get_field(ctx, "front_left", "ankle", &found);
        assert(found && front_left_ankle.type == CXPR_VALUE_NUMBER);
        assert(front_left_knee.d > 0.0);
        assert(front_left_ankle.d > 0.0);

        front_left_joint_x = cxpr_context_get_field(ctx, "front_left_joint", "x", &found);
        assert(found && front_left_joint_x.type == CXPR_VALUE_NUMBER);
        assert(fabs(front_left_joint_x.d - front_left_coxa.d) < 1e-12);

        if (i == 0) {
            first_coxa = front_left_coxa.d;
        } else if (i == 1) {
            assert(fabs(front_left_coxa.d - first_coxa) > 1e-6);
        }
    }

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(robot_source);
    free(robotics_source);
    free(source);
    printf("  ✓ test_robot_hexapod_fixture_simulates_vec3_io\n");
}

static void test_advanced_strategy_syntax_compiles_and_ticks(void) {
    cxpr_error err = {0};
    bool found = false;
    bool value = false;
    cxpr_model* model = parse_model_ok(
        "name advanced_strategy\n"
        "use math\n"
        "use finance\n"
        "in {\n"
        "    close,\n"
        "    avg,\n"
        "    position\n"
        "}\n"
        "$threshold = 1.5\n"
        "$decay = 0.25\n"
        "fn over(src, base, threshold) = src - base > threshold\n"
        "fn impulse(src, base, threshold) {\n"
        "    delta = src - base\n"
        "    magnitude = abs(delta)\n"
        "    passed = over(src, base, threshold)\n"
        "    return passed and magnitude > threshold\n"
        "}\n"
        "state {\n"
        "    score = 0\n"
        "}\n"
        "raw =\n"
        "    impulse(close, avg, $threshold)\n"
        "update state {\n"
        "    score = score * $decay + close - avg\n"
        "}\n"
        "buy =\n"
        "    raw and not position\n"
        "out {\n"
        "    buy,\n"
        "    score\n"
        "}\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_model_session* session;
    cxpr_context* ctx;

    if (!program) {
        fprintf(stderr, "advanced compile failed: %s\n", err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    assert(cxpr_model_use_count(model) == 2);
    assert(cxpr_model_input_count(model) == 3);
    assert(cxpr_model_constant_count(model) == 2);
    assert(cxpr_model_binding_count(model) == 4);
    assert(cxpr_model_output_count(model) == 2);
    assert(cxpr_model_program_function_count(program) == 3);

    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 10.0);
    cxpr_context_set(ctx, "avg", 8.0);
    cxpr_context_set_bool(ctx, "position", false);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "score", &found) == 2.0 && found);
    assert(cxpr_model_session_output_bool(session, "buy", &value));
    assert(value);
    assert(cxpr_model_session_output_rising(session, "buy"));

    cxpr_context_set(ctx, "close", 8.5);
    cxpr_context_set(ctx, "avg", 8.0);
    cxpr_context_set_bool(ctx, "position", false);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(cxpr_context_get(ctx, "score", &found) == 1.0 && found);
    assert(cxpr_model_session_output_bool(session, "buy", &value));
    assert(!value);
    assert(cxpr_model_session_output_falling(session, "buy"));

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_advanced_strategy_syntax_compiles_and_ticks\n");
}

static void test_function_record_return_compiles_with_field_access(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name record_fn\n"
        "in { close }\n"
        "fn bands(src) {\n"
        "    upper = src + 1\n"
        "    lower = src - 1\n"
        "    return { upper, lower }\n"
        "}\n"
        "top = bands(close).upper\n"
        "bottom = bands(close).lower\n"
        "wide = top - bottom\n"
        "out { top, bottom, wide }\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();

    if (!program) {
        fprintf(stderr, "record compile failed: %s\n", err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "top", &found) == 11.0 && found);
    assert(cxpr_context_get(ctx, "bottom", &found) == 9.0 && found);
    assert(cxpr_context_get(ctx, "wide", &found) == 2.0 && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_function_record_return_compiles_with_field_access\n");
}

static void test_function_record_shorthand_compiles_with_field_access(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name record_fn_shorthand\n"
        "in { close }\n"
        "fn vec3(x, y, z) = x, y, z\n"
        "fn bands(src) {\n"
        "    upper = src + 1\n"
        "    lower = src - 1\n"
        "    return upper, lower\n"
        "}\n"
        "top = bands(close).upper\n"
        "bottom = bands(close).lower\n"
        "point = vec3(top, bottom, close)\n"
        "out { top, bottom, point }\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();

    if (!program) {
        fprintf(stderr, "record shorthand compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get(ctx, "top", &found) == 11.0 && found);
    assert(cxpr_context_get(ctx, "bottom", &found) == 9.0 && found);
    assert(cxpr_context_get_field(ctx, "point", "x", &found).d == 11.0 && found);
    assert(cxpr_context_get_field(ctx, "point", "y", &found).d == 9.0 && found);
    assert(cxpr_context_get_field(ctx, "point", "z", &found).d == 10.0 && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_function_record_shorthand_compiles_with_field_access\n");
}

static void test_cxta_signal_compat_functions(void) {
    cxpr_error err = {0};
    bool found = false;
    cxpr_model* model = parse_model_ok(
        "name cxta_signal_compat\n"
        "in { value, from, to, cur_left, cur_right, prev_left, prev_right }\n"
        "fn above(left, right) = left > right\n"
        "fn below(left, right) = left < right\n"
        "fn score(value, from, to) =\n"
        "    if(from == to,\n"
        "       0,\n"
        "       if(from < to,\n"
        "          if(value <= from, 0, if(value >= to, 1, (value - from) / (to - from))),\n"
        "          if(value >= from, 0, if(value <= to, 1, (from - value) / (from - to)))))\n"
        "fn cross_above4(cur_left, cur_right, prev_left, prev_right) =\n"
        "    prev_left <= prev_right and cur_left > cur_right\n"
        "fn cross_below4(cur_left, cur_right, prev_left, prev_right) =\n"
        "    prev_left >= prev_right and cur_left < cur_right\n"
        "is_above = above(cur_left, cur_right)\n"
        "is_below = below(cur_left, cur_right)\n"
        "score_value = score(value, from, to)\n"
        "cross_up = cross_above4(cur_left, cur_right, prev_left, prev_right)\n"
        "cross_down = cross_below4(cur_left, cur_right, prev_left, prev_right)\n"
        "out { is_above, is_below, score_value, cross_up, cross_down }\n");
    cxpr_model_program* program = cxpr_compile_model(model, NULL, &err);
    cxpr_context* ctx = cxpr_context_new();

    assert(program != NULL);
    assert(ctx != NULL);

    /* Matches cxta_signal_score({5,0,10}) and cxta_signal_cross_above({3,2,1,2}). */
    cxpr_context_set(ctx, "value", 5.0);
    cxpr_context_set(ctx, "from", 0.0);
    cxpr_context_set(ctx, "to", 10.0);
    cxpr_context_set(ctx, "cur_left", 3.0);
    cxpr_context_set(ctx, "cur_right", 2.0);
    cxpr_context_set(ctx, "prev_left", 1.0);
    cxpr_context_set(ctx, "prev_right", 2.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(cxpr_context_get_bool(ctx, "is_above", &found) && found);
    assert(!cxpr_context_get_bool(ctx, "is_below", &found) && found);
    assert(cxpr_context_get(ctx, "score_value", &found) == 0.5 && found);
    assert(cxpr_context_get_bool(ctx, "cross_up", &found) && found);
    assert(!cxpr_context_get_bool(ctx, "cross_down", &found) && found);

    /* Matches cxta_signal_score({5,10,0}) and cxta_signal_cross_below({1,2,3,2}). */
    cxpr_context_set(ctx, "from", 10.0);
    cxpr_context_set(ctx, "to", 0.0);
    cxpr_context_set(ctx, "cur_left", 1.0);
    cxpr_context_set(ctx, "cur_right", 2.0);
    cxpr_context_set(ctx, "prev_left", 3.0);
    cxpr_context_set(ctx, "prev_right", 2.0);
    assert(cxpr_eval_model_program(program, ctx, NULL, &err));
    assert(!cxpr_context_get_bool(ctx, "is_above", &found) && found);
    assert(cxpr_context_get_bool(ctx, "is_below", &found) && found);
    assert(cxpr_context_get(ctx, "score_value", &found) == 0.5 && found);
    assert(!cxpr_context_get_bool(ctx, "cross_up", &found) && found);
    assert(cxpr_context_get_bool(ctx, "cross_down", &found) && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    printf("  ✓ test_cxta_signal_compat_functions\n");
}

static void test_yaml_converted_ensemble_strategy_fixture(void) {
    cxpr_error err = {0};
    bool found = false;
    char* source = read_fixture("fixtures/strategies/ensemble_score_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_context* ctx;

    assert(source != NULL);
    model = parse_model_ok(source);
    program = cxpr_compile_model(model, NULL, &err);
    ctx = cxpr_context_new();

    if (!program) {
        fprintf(stderr, "ensemble fixture compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    assert(ctx != NULL);
    assert(cxpr_model_program_seed_defaults(program, ctx, NULL, &err));

    cxpr_context_set(ctx, "close", 100.0);
    cxpr_context_set(ctx, "ema_f", 105.0);
    cxpr_context_set(ctx, "ema_s", 100.0);
    cxpr_context_set(ctx, "macd_histogram", 0.35);
    cxpr_context_set(ctx, "rsi_value", 50.0);
    cxpr_context_set(ctx, "bb_lower", 90.0);
    cxpr_context_set(ctx, "volume", 180.0);
    cxpr_context_set(ctx, "vol_ma", 100.0);
    cxpr_context_set(ctx, "atr_value", 4.0);

    if (!cxpr_eval_model_program(program, ctx, NULL, &err)) {
        fprintf(stderr, "ensemble fixture eval failed: %s\n",
                err.message ? err.message : "(null)");
        assert(0);
    }
    assert(cxpr_context_get_bool(ctx, "entry", &found) && found);
    assert(!cxpr_context_get_bool(ctx, "exit", &found) && found);
    assert(fabs(cxpr_context_get(ctx, "entry_score", &found) - 0.9) < 1e-12 && found);
    assert(fabs(cxpr_context_get(ctx, "exit_score", &found) - 0.04) < 1e-12 && found);

    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    printf("  ✓ test_yaml_converted_ensemble_strategy_fixture\n");
}

static void test_rsi_state_strategy_fixture(void) {
    cxpr_error err = {0};
    bool found = false;
    bool value = false;
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(source != NULL);
    model = parse_model_ok(source);
    program = cxpr_compile_model(model, NULL, &err);
    if (!program) {
        fprintf(stderr, "rsi fixture compile failed: %s\n",
                err.message ? err.message : "(null)");
    }
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);

    cxpr_context_set(ctx, "trend", 99.0);
    cxpr_context_set(ctx, "close", 100.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(fabs(cxpr_context_get(ctx, "r", &found) - 50.0) < 1e-12 && found);
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 102.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(fabs(cxpr_context_get(ctx, "r", &found) - 50.0) < 1e-12 && found);
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 104.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(fabs(cxpr_context_get(ctx, "r", &found) - 50.0) < 1e-12 && found);
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(!value);

    cxpr_context_set(ctx, "close", 106.0);
    assert(cxpr_model_session_tick(program, session, NULL, &err));
    assert(fabs(cxpr_context_get(ctx, "r", &found) - 100.0) < 1e-12 && found);
    assert(fabs(cxpr_context_get(ctx, "avg_gain", &found) - 2.0) < 1e-12 && found);
    assert(fabs(cxpr_context_get(ctx, "avg_loss", &found) - 0.0) < 1e-12 && found);
    assert(cxpr_model_session_output_bool(session, "entry", &value));
    assert(value);
    assert(cxpr_model_session_output_rising(session, "entry"));

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    printf("  ✓ test_rsi_state_strategy_fixture\n");
}

static void test_rsi_state_strategy_fixture_emits_c_tick(void) {
    cxpr_error err = {0};
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    char* code;
    char* specialized_code;
    const double params[] = {3.0, 60.0, 45.0};

    assert(source != NULL);
    model = parse_model_ok(source);
    program = cxpr_compile_model(model, NULL, &err);
    assert(program != NULL);
    assert(cxpr_model_program_uses_fused_ir(program));
    assert(cxpr_model_program_c_slot_count(program) > 0u);
    assert(cxpr_model_program_c_param_count(program) == 3u);
    assert(strcmp(cxpr_model_program_c_param_name(program, 0u), "rsi_period") == 0);

    code = cxpr_model_program_to_c_tick_function(program, "static inline",
                                                 "cxpr_rsi_state_tick", &err);
    if (!code) fprintf(stderr, "model C tick emit failed: %s\n", err.message);
    assert(code != NULL);
    assert(strstr(code, "static inline void cxpr_rsi_state_tick(double* restrict _cx_slots, const double* restrict _cx_inputs, const double* restrict _cx_params, double* restrict _cx_outputs)"));
    assert(strstr(code, "static inline double cxpr_fn_cxpr_rsi_state_tick_rsi"));
    assert(strstr(code, "const double change ="));
    assert(strstr(code, "cxpr_fn_cxpr_rsi_state_tick_rsi(next_avg_gain, next_avg_loss)"));
    assert(!strstr(code, "fmax("));
    assert(!strstr(code, "_cx_v0"));
    assert(!strstr(code, "L0:"));
    assert(strstr(code, "_cx_slots["));
    assert(strstr(code, "_cx_params["));
    assert(strstr(code, "_cx_outputs["));
    assert(!strstr(code, "cxpr_registry"));

    specialized_code = cxpr_model_program_to_c_tick_function_with_params(
        program, "static inline", "cxpr_rsi_state_tick_specialized",
        params, 3u, &err);
    if (!specialized_code) {
        fprintf(stderr, "model specialized C tick emit failed: %s\n", err.message);
    }
    assert(specialized_code != NULL);
    assert(strstr(specialized_code, "static inline void cxpr_rsi_state_tick_specialized"));
    assert(strstr(specialized_code, "const double gain = ((change > 0.0) ? (change) : (0.0))"));
    assert(!strstr(specialized_code, "_cx_params["));
    assert(strstr(specialized_code, "60"));
    assert(strstr(specialized_code, "45"));

    free(specialized_code);
    free(code);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    printf("  ✓ test_rsi_state_strategy_fixture_emits_c_tick\n");
}

#ifdef CXPR_TEST_WITH_CXTA
static void test_rsi_state_strategy_fixture_matches_cxta(void) {
    const double closes[] = {100.0, 102.0, 101.0, 104.0, 103.0, 106.0, 105.0, 107.0};
    const size_t close_count = sizeof(closes) / sizeof(closes[0]);
    cxta_series_bar bars[sizeof(closes) / sizeof(closes[0])] = {{0}};
    cxpr_error err = {0};
    bool found = false;
    char* source = read_fixture("fixtures/strategies/rsi_state_model.cxpr");
    cxpr_model* model;
    cxpr_model_program* program;
    cxpr_model_session* session;
    cxpr_context* ctx;

    assert(source != NULL);
    model = parse_model_ok(source);
    program = cxpr_compile_model(model, NULL, &err);
    assert(program != NULL);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session != NULL);
    ctx = cxpr_model_session_context(session);
    assert(ctx != NULL);
    cxpr_context_set(ctx, "trend", 0.0);

    for (size_t i = 0; i < close_count; ++i) {
        cxta_series_bar_view view;
        double cxpr_r;
        double cxta_r;
        bars[i].close = closes[i];
        view = cxta_series_bar_view_make(bars, i + 1u, i);
        cxpr_context_set(ctx, "close", closes[i]);
        assert(cxpr_model_session_tick(program, session, NULL, &err));
        cxpr_r = cxpr_context_get(ctx, "r", &found);
        assert(found);
        cxta_r = cxta_rsi(&view, 3);
        assert(fabs(cxpr_r - cxta_r) < 1e-9);
    }

    cxpr_model_session_free(session);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(source);
    printf("  ✓ test_rsi_state_strategy_fixture_matches_cxta\n");
}
#endif

int main(void) {
    printf("Running cxpr model parser tests...\n");
    test_parse_minimal_strategy_model();
    test_parse_model_with_declaration_metadata_blocks();
    test_parse_model_rejects_legacy_meta_block();
    test_parse_model_rejects_decorator_metadata();
    test_parse_model_host_blocks_preserve_body();
    test_parse_model_rejects_yaml_mapping_in_host_block();
    test_parse_model_rejects_meta_as_host_block();
    test_model_plan_bind_sources_exports_scoped_timeframes();
    test_parse_state_init_and_update_state();
    test_reject_legacy_state_assignment_syntax();
    test_parse_out_expr_assignment_adds_output();
    test_parse_update_state_does_not_add_output();
    test_parse_update_state_with_explicit_output();
    test_update_state_supports_block_local_temporaries();
    test_reject_legacy_out_state_update_syntax();
    test_validate_rejects_duplicate_state_declaration();
    test_reject_invalid_out_assignment_name();
    test_reject_invalid_output_name();
    test_validate_accepts_known_symbols();
    test_validate_rejects_unknown_output();
    test_validate_rejects_unknown_references();
    test_validate_rejects_unknown_constants();
    test_validate_rejects_duplicate_symbols();
    test_eval_order_uses_existing_expression_toposort();
    test_eval_order_rejects_cycles();
    test_compile_and_eval_model_program();
    test_compile_model_defined_function_without_host_registration();
    test_output_list_syntax();
    test_function_block_out_expression();
    test_state_session_atomic_commit_and_events();
    test_session_input_lookback();
    test_session_expression_lookback();
    test_session_record_field_lookback();
    test_session_direct_record_producer_lookback();
    test_macd_record_cross_strategy_fixture();
    test_robot_hexapod_fixture_simulates_vec3_io();
    test_advanced_strategy_syntax_compiles_and_ticks();
    test_function_record_return_compiles_with_field_access();
    test_function_record_shorthand_compiles_with_field_access();
    test_cxta_signal_compat_functions();
    test_yaml_converted_ensemble_strategy_fixture();
    test_rsi_state_strategy_fixture();
    test_rsi_state_strategy_fixture_emits_c_tick();
#ifdef CXPR_TEST_WITH_CXTA
    test_rsi_state_strategy_fixture_matches_cxta();
#endif
    printf("All model parser tests passed.\n");
    return 0;
}
