#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/cxpr.h>

// Minimal provider metadata used by the plan driver. `close` is a scoped direct
// source, `ema` is a source-input function, and `atr` is a scoped indicator with
// one numeric bound argument.
static const cxpr_provider_scope_spec timeframe_scope = {"timeframe", true};
static const cxpr_provider_source_spec close_source = {"close", 0u, 1u, &timeframe_scope};
static const cxpr_provider_source_spec* const sources[] = {&close_source};
static const cxpr_provider_param_descriptor ema_params[] = {{"period"}};
static const cxpr_provider_param_descriptor atr_params[] = {{"period"}};
static const cxpr_provider_fn_spec ema_spec = {
    .name = "ema",
    .min_args = 1u,
    .max_args = 1u,
    .source_min_args = 1u,
    .source_max_args = 1u,
    .params = ema_params,
    .param_count = CXPR_ARRAY_COUNT(ema_params),
    .fields = NULL,
    .field_count = 0u,
    .primary_field_index = -1,
    .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
    .scope = &timeframe_scope,
};
static const cxpr_provider_fn_spec atr_spec = {
    .name = "atr",
    .min_args = 1u,
    .max_args = 1u,
    .source_min_args = 0u,
    .source_max_args = 0u,
    .params = atr_params,
    .param_count = CXPR_ARRAY_COUNT(atr_params),
    .fields = NULL,
    .field_count = 0u,
    .primary_field_index = -1,
    .flags = 0u,
    .scope = &timeframe_scope,
};
static const cxpr_provider_fn_spec* const funcs[] = {&ema_spec, &atr_spec};

static const cxpr_provider_fn_spec* const* test_fn_specs(const void* userdata, size_t* count) {
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(funcs);
    return funcs;
}

static const cxpr_provider_fn_spec* test_fn_spec_find(const void* userdata, const char* name) {
    size_t i;
    (void)userdata;
    for (i = 0u; i < CXPR_ARRAY_COUNT(funcs); ++i) {
        if (strcmp(funcs[i]->name, name) == 0) return funcs[i];
    }
    return NULL;
}

static const cxpr_provider_source_spec* const* test_source_specs(const void* userdata, size_t* count) {
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(sources);
    return sources;
}

static const cxpr_provider_source_spec* test_source_spec_find(const void* userdata, const char* name) {
    size_t i;
    (void)userdata;
    for (i = 0u; i < CXPR_ARRAY_COUNT(sources); ++i) {
        if (strcmp(sources[i]->name, name) == 0) return sources[i];
    }
    return NULL;
}

static const cxpr_provider_vtable provider_vtable = {
    .fn_specs = test_fn_specs,
    .fn_spec_find = test_fn_spec_find,
    .source_specs = test_source_specs,
    .source_spec_find = test_source_spec_find,
    .expr_param_spec_for = NULL,
};

static const cxpr_provider provider = {
    .name = "source_plan_bind_test",
    .userdata = NULL,
    .vtable = &provider_vtable,
};

// Captures every bind/resolve call so the tests can assert what cxpr planned
// without depending on any real market-data registry.
typedef struct {
    size_t call_count;
    size_t resolve_count;
    char names[8][32];
    char scopes[8][32];
    size_t arg_counts[8];
    double first_args[8];
} bind_capture;

// Simulates a host source registry. The plan driver passes structured
// source-plan nodes here; the host maps each node to a concrete handle.
static int capture_bind(
    const cxpr_source_plan_node* node,
    const double* bound_args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata) {
    bind_capture* capture = (bind_capture*)userdata;
    size_t index;

    assert(capture != NULL);
    assert(node != NULL);
    assert(out_handle != NULL);
    assert(capture->call_count < CXPR_ARRAY_COUNT(capture->names));

    index = capture->call_count++;
    snprintf(capture->names[index], sizeof(capture->names[index]), "%s", node->name ? node->name : "");
    snprintf(capture->scopes[index], sizeof(capture->scopes[index]), "%s", node->scope_value ? node->scope_value : "");
    capture->arg_counts[index] = arg_count;
    capture->first_args[index] = arg_count > 0u ? bound_args[0] : NAN;

    if (strcmp(node->name, "close") == 0 &&
        node->scope_value &&
        strcmp(node->scope_value, "1d") == 0) {
        *out_handle = 11u;
        return 1;
    }
    if (strcmp(node->name, "close") == 0 &&
        node->scope_value &&
        strcmp(node->scope_value, "1h") == 0) {
        *out_handle = 12u;
        return 1;
    }
    if (strcmp(node->name, "atr") == 0 &&
        node->scope_value &&
        strcmp(node->scope_value, "1d") == 0 &&
        arg_count == 1u &&
        fabs(bound_args[0] - 14.0) < 1e-12) {
        *out_handle = 114u;
        return 1;
    }
    return 0;
}

// Simulates eval-time lookup for a scoped source handle registered by the plan
// driver. This proves registration and resolver wiring both happened.
static int capture_resolve(
    uint64_t handle,
    const char* source_name,
    double* out_value,
    void* userdata) {
    bind_capture* capture = (bind_capture*)userdata;
    assert(capture != NULL);
    assert(source_name != NULL);
    assert(out_value != NULL);
    capture->resolve_count++;
    if (handle == 11u && strcmp(source_name, "close") == 0) {
        *out_value = 123.0;
        return 1;
    }
    return 0;
}

typedef struct {
    const double* hourly_close;
    size_t hourly_count;
    const double* daily_close;
    size_t daily_count;
    size_t current_index;
    size_t bind_count;
    size_t resolve_count;
} bar_series_store;

// Simulates the bind step a trading host would do after reading its data-source
// configuration. The source-plan node tells the host which source/scope was
// requested; the host returns a stable handle for that concrete series.
static int bar_series_bind(
    const cxpr_source_plan_node* node,
    const double* bound_args,
    size_t arg_count,
    uint64_t* out_handle,
    void* userdata) {
    bar_series_store* store = (bar_series_store*)userdata;

    (void)bound_args;
    assert(store != NULL);
    assert(node != NULL);
    assert(out_handle != NULL);
    assert(arg_count == 0u);
    store->bind_count++;

    if (strcmp(node->name, "close") != 0 || !node->scope_value) return 0;
    if (strcmp(node->scope_value, "1h") == 0) {
        *out_handle = 1001u;
        return 1;
    }
    if (strcmp(node->scope_value, "1d") == 0) {
        *out_handle = 2001u;
        return 1;
    }
    return 0;
}

// Simulates bar-by-bar source resolution. The handle selects the materialized
// series, while current_index selects the active bar in that series.
static int bar_series_resolve(
    uint64_t handle,
    const char* source_name,
    double* out_value,
    void* userdata) {
    bar_series_store* store = (bar_series_store*)userdata;

    assert(store != NULL);
    assert(source_name != NULL);
    assert(out_value != NULL);
    if (strcmp(source_name, "close") != 0) return 0;
    store->resolve_count++;

    if (handle == 1001u && store->current_index < store->hourly_count) {
        *out_value = store->hourly_close[store->current_index];
        return 1;
    }
    if (handle == 2001u && store->current_index < store->daily_count) {
        *out_value = store->daily_close[store->current_index];
        return 1;
    }
    return 0;
}

static cxpr_expr_ast* parse_or_die(cxpr_parser* parser, const char* text) {
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, text, &err);
    if (!ast) {
        fprintf(stderr, "parse failed: %s\n", err.message ? err.message : "(no message)");
        abort();
    }
    return ast;
}

static void test_plan_bind_sources_uses_callback_for_source_plan_leaves(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_ast* ast;
    cxpr_source_plan_bindings bindings = {0};
    bind_capture capture = {0};
    cxpr_plan_config config;
    cxpr_error err = {0};
    double resolved = 0.0;

    assert(parser != NULL);
    assert(reg != NULL);
    assert(ctx != NULL);
    cxpr_register_defaults(reg);

    // `$period` should be evaluated by cxpr before the bind callback sees the
    // matching source-plan node.
    cxpr_context_set_param(ctx, "period", 9.0);

    // This expression exercises three materializable leaves:
    // 1. direct scoped source: close(timeframe="1d")
    // 2. child source inside a source-input function: close(timeframe="1h")
    // 3. scoped indicator with a numeric bound arg: atr(14, timeframe="1d")
    ast = parse_or_die(
        parser,
        "close(timeframe=\"1d\") + ema(close(timeframe=\"1h\"), $period) + atr(14, timeframe=\"1d\")");

    // One userdata pointer is shared by plan-time binding and eval-time
    // resolving. Real hosts usually point this at their data-source registry.
    config = (cxpr_plan_config){
        .bind = capture_bind,
        .resolve = capture_resolve,
        .userdata = &capture,
    };

    // The plan driver owns traversal, source-plan parsing, numeric arg
    // evaluation, and scoped-source function registration. The test host only
    // maps source-plan leaves to handles in capture_bind().
    assert(cxpr_plan_bind_sources(
        &provider,
        ast,
        ctx,
        reg,
        &config,
        &bindings,
        &err));

    // The callback should see the three leaves in traversal order and return
    // the handles captured in `bindings`.
    assert(capture.call_count == 3u);
    assert(bindings.count == 3u);
    assert(bindings.handles[0] == 11u);
    assert(bindings.handles[1] == 12u);
    assert(bindings.handles[2] == 114u);
    assert(strcmp(capture.names[0], "close") == 0);
    assert(strcmp(capture.scopes[0], "1d") == 0);
    assert(capture.arg_counts[0] == 0u);
    assert(strcmp(capture.names[1], "close") == 0);
    assert(strcmp(capture.scopes[1], "1h") == 0);
    assert(capture.arg_counts[1] == 0u);
    assert(strcmp(capture.names[2], "atr") == 0);
    assert(strcmp(capture.scopes[2], "1d") == 0);
    assert(capture.arg_counts[2] == 1u);
    assert(fabs(capture.first_args[2] - 14.0) < 1e-12);
    {
        // cxpr_plan_bind_sources auto-registers scoped source functions from
        // provider source metadata. A runtime call with the planned handle
        // should therefore delegate to capture_resolve().
        cxpr_expr_ast* runtime_ast = parse_or_die(parser, "close(11)");
        assert(cxpr_eval_ast_number(runtime_ast, ctx, reg, &resolved, &err));
        assert(fabs(resolved - 123.0) < 1e-12);
        assert(capture.resolve_count == 1u);
        cxpr_expr_ast_free(runtime_ast);
    }

    cxpr_free_source_plan_bindings(&bindings);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
}

static void test_plan_bound_sources_resolve_against_bars(void) {
    const double close_1h[] = {9.0, 10.2, 10.1, 12.0};
    const double close_1d[] = {9.5, 10.0, 10.5, 11.0};
    const bool expected_signal[] = {false, true, false, true};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_ast* plan_ast;
    cxpr_expr_ast* runtime_ast;
    cxpr_source_plan_bindings bindings = {0};
    bar_series_store store = {
        .hourly_close = close_1h,
        .hourly_count = CXPR_ARRAY_COUNT(close_1h),
        .daily_close = close_1d,
        .daily_count = CXPR_ARRAY_COUNT(close_1d),
    };
    cxpr_plan_config config = {
        .bind = bar_series_bind,
        .resolve = bar_series_resolve,
        .userdata = &store,
    };
    cxpr_error err = {0};
    size_t i;

    assert(parser != NULL);
    assert(reg != NULL);
    assert(ctx != NULL);
    cxpr_register_defaults(reg);

    // This is the host-facing expression. Planning sees scoped source calls and
    // binds them to concrete handles, but this test does not require cxpr to
    // rewrite the AST itself.
    plan_ast = parse_or_die(parser, "close(timeframe=\"1h\") > close(timeframe=\"1d\")");
    assert(cxpr_plan_bind_sources(
        &provider,
        plan_ast,
        ctx,
        reg,
        &config,
        &bindings,
        &err));
    assert(store.bind_count == 2u);
    assert(bindings.count == 2u);
    assert(bindings.handles[0] == 1001u);
    assert(bindings.handles[1] == 2001u);

    // A real host/compiler would use the bindings when lowering the planned
    // expression. The handle-shaped runtime expression below exercises the
    // same eval-time path: close(handle) -> resolver -> active bar value.
    runtime_ast = parse_or_die(parser, "close(1001) > close(2001)");
    for (i = 0u; i < CXPR_ARRAY_COUNT(expected_signal); ++i) {
        bool signal = false;
        store.current_index = i;
        assert(cxpr_eval_ast_bool(runtime_ast, ctx, reg, &signal, &err));
        assert(signal == expected_signal[i]);
    }
    assert(store.resolve_count == CXPR_ARRAY_COUNT(expected_signal) * 2u);

    cxpr_expr_ast_free(runtime_ast);
    cxpr_free_source_plan_bindings(&bindings);
    cxpr_expr_ast_free(plan_ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
}

static void test_plan_bind_sources_from_table_maps_name_and_scope(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_ast* ast;
    cxpr_source_plan_bindings bindings = {0};
    cxpr_error err = {0};

    // Convenience API for simple hosts: bind by source name + optional scope
    // without writing a callback.
    const cxpr_source_handle_entry table[] = {
        {"close", "1d", 21u},
        {"close", "", 22u},
    };

    assert(parser != NULL);
    assert(reg != NULL);
    assert(ctx != NULL);
    cxpr_register_defaults(reg);

    // The first leaf is scoped; the second is the provider's default/unscoped
    // close source. The table must distinguish those handles.
    ast = parse_or_die(parser, "close(timeframe=\"1d\") + close");

    assert(cxpr_plan_bind_sources_from_table(
        &provider,
        ast,
        ctx,
        reg,
        table,
        CXPR_ARRAY_COUNT(table),
        &bindings,
        &err));
    assert(bindings.count == 2u);
    assert(bindings.handles[0] == 21u);
    assert(bindings.handles[1] == 22u);

    cxpr_free_source_plan_bindings(&bindings);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
}

int main(void) {
    test_plan_bind_sources_uses_callback_for_source_plan_leaves();
    test_plan_bound_sources_resolve_against_bars();
    test_plan_bind_sources_from_table_maps_name_and_scope();
    printf("source plan bind tests passed\n");
    return 0;
}
