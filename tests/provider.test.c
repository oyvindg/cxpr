#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/provider.h>
#include <cxpr/typecheck.h>

static const cxpr_provider_fn_spec* const* test_provider_fn_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_param_descriptor consumer_params[] = {
        {"period"},
    };
    static const cxpr_provider_scope_spec scope = {
        "resolution",
        1,
    };
    static const cxpr_provider_fn_spec consumer = {
        .name = "moving_average",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 1u,
        .source_max_args = 1u,
        .params = consumer_params,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
        .scope = &scope,
    };
    static const cxpr_provider_fn_spec* const specs[] = {
        &consumer,
    };

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_fn_spec* test_provider_fn_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_fn_spec* const* specs = test_provider_fn_specs(userdata, &count);
    size_t i;

    for (i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static const cxpr_provider_source_spec* const* test_provider_source_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_scope_spec scope = {
        "resolution",
        1,
    };
    static const cxpr_provider_source_spec source = {
        "temperature",
        0u,
        1u,
        &scope,
    };
    static const cxpr_provider_source_spec* const specs[] = {
        &source,
    };

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_source_spec* test_provider_source_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_source_spec* const* specs =
        test_provider_source_specs(userdata, &count);
    size_t i;

    for (i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static void test_provider_helpers_support_generic_series_scopes(void) {
    static const cxpr_provider_vtable vtable = {
        .fn_specs = test_provider_fn_specs,
        .fn_spec_find = test_provider_fn_spec_find,
        .source_specs = test_provider_source_specs,
        .source_spec_find = test_provider_source_spec_find,
        .expr_param_spec_for = NULL,
    };
    static const cxpr_provider provider = {
        "generic-metrics",
        NULL,
        &vtable,
    };
    size_t fn_count = 0u;
    size_t source_count = 0u;
    const cxpr_provider_fn_spec* fn;
    const cxpr_provider_source_spec* source;

    if (cxpr_provider_is_valid(&provider) == 0) abort();
    if (cxpr_provider_fn_specs(&provider, &fn_count) == NULL || fn_count != 1u) abort();
    if (cxpr_provider_source_specs(&provider, &source_count) == NULL ||
        source_count != 1u) {
        abort();
    }

    fn = cxpr_provider_fn_spec_find(&provider, "moving_average");
    if (!fn || !fn->scope || strcmp(fn->scope->param_name, "resolution") != 0) abort();

    source = cxpr_provider_source_spec_find(&provider, "temperature");
    if (!source || !source->scope || strcmp(source->scope->param_name, "resolution") != 0) abort();

}

static double test_runtime_required_scalar(
    const char* name,
    const double* args,
    size_t argc,
    void* userdata) {
    (void)userdata;
    if (strcmp(name, "record_fn") == 0) {
        return argc > 0u ? args[0] : 0.0;
    }
    if (strcmp(name, "record_fn.value") == 0) {
        return argc > 0u ? args[0] + 10.0 : 10.0;
    }
    if (strcmp(name, "record_fn.signal") == 0) {
        return argc > 1u ? args[1] + 20.0 : 20.0;
    }
    return NAN;
}

static const cxpr_provider_fn_spec* const* test_record_provider_fn_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_param_descriptor params[] = {
        {"fast"},
        {"slow"},
    };
    static const cxpr_provider_field_descriptor fields[] = {
        {"value"},
        {"signal"},
    };
    static const cxpr_provider_fn_spec record_fn = {
        .name = "record_fn",
        .min_args = 1u,
        .max_args = 2u,
        .params = params,
        .param_count = CXPR_ARRAY_COUNT(params),
        .fields = fields,
        .field_count = CXPR_ARRAY_COUNT(fields),
        .primary_field_index = 0,
        .flags = CXPR_PROVIDER_FN_RECORD_OUTPUT,
    };
    static const cxpr_provider_fn_spec* const specs[] = {
        &record_fn,
    };
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_fn_spec* test_record_provider_fn_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_fn_spec* const* specs =
        test_record_provider_fn_specs(userdata, &count);
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static void test_provider_signatures_register_record_output_struct_producer(void) {
    static const cxpr_provider_vtable vtable = {
        .fn_specs = test_record_provider_fn_specs,
        .fn_spec_find = test_record_provider_fn_spec_find,
        .source_specs = test_provider_source_specs,
        .source_spec_find = test_provider_source_spec_find,
        .expr_param_spec_for = NULL,
    };
    static const cxpr_provider provider = {
        "record-test",
        NULL,
        &vtable,
    };
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* prog;
    cxpr_value value = {0};

    assert(reg != NULL);
    assert(parser != NULL);
    assert(ctx != NULL);

    cxpr_register_provider_signatures(
        reg,
        &provider,
        &(const cxpr_host_config){
            .runtime_required_scalar = test_runtime_required_scalar,
        });

    ast = cxpr_expr_ast_parse(parser, "record_fn(3, 5).signal", &err);
    if (ast == NULL) abort();
    prog = cxpr_expr_compile(ast, reg, &err);
    if (prog == NULL) abort();
    if (!cxpr_expr_compiled_eval(prog, ctx, reg, &value, &err)) abort();
    if (err.code != CXPR_OK || value.type != CXPR_VALUE_NUMBER || value.d != 25.0) abort();

    cxpr_expr_compiled_free(prog);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_expr_parser_free(parser);
    cxpr_registry_free(reg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Source-plan EXPRESSION node test fixtures
 * ═══════════════════════════════════════════════════════════════════════════ */

static const cxpr_provider_scope_spec expr_scope = {
    "selector",
    1,
};

static const cxpr_provider_fn_spec* const* expr_provider_fn_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_param_descriptor ema_params[] = {{"period"}};
    static const cxpr_provider_param_descriptor atr_params[] = {{"period"}};
    static const cxpr_provider_fn_spec ema = {
        .name = "ema",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 1u,
        .source_max_args = 1u,
        .params = ema_params,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
        .scope = &expr_scope,
    };
    static const cxpr_provider_fn_spec atr = {
        .name = "atr",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 0u,
        .source_max_args = 0u,
        .params = atr_params,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = 0u,
        .scope = NULL,
    };
    static const cxpr_provider_fn_spec* const specs[] = {&ema, &atr};

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_fn_spec* expr_provider_fn_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_fn_spec* const* specs = expr_provider_fn_specs(userdata, &count);
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static const cxpr_provider_source_spec* const* expr_provider_source_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_source_spec close = {"close", 0u, 1u, &expr_scope};
    static const cxpr_provider_source_spec high  = {"high",  0u, 1u, &expr_scope};
    static const cxpr_provider_source_spec low   = {"low",   0u, 1u, &expr_scope};
    static const cxpr_provider_source_spec* const specs[] = {&close, &high, &low};
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_source_spec* expr_provider_source_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_source_spec* const* specs =
        expr_provider_source_specs(userdata, &count);
    size_t i;
    for (i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static int expr_provider_expr_param_spec_for(
    const void* userdata,
    const char* name,
    cxpr_expr_param_spec* out) {
    static const char* const ema_names[] = {"source", "period"};
    static const cxpr_expr_arg_kind ema_kinds[] = {
        CXPR_EXPR_ARG_SCALAR_SOURCE,
        CXPR_EXPR_ARG_NUMERIC,
    };
    (void)userdata;
    if (!name || !out || strcmp(name, "ema") != 0) return 0;
    *out = (cxpr_expr_param_spec){
        .names = ema_names,
        .defaults = NULL,
        .kinds = ema_kinds,
        .count = CXPR_ARRAY_COUNT(ema_names),
        .min_count = 2u,
        .lookback_sugar_name = NULL,
        .has_timeframe_param = 0,
    };
    return 1;
}

static const cxpr_provider expr_provider = {
    "expr-test",
    NULL,
    &(const cxpr_provider_vtable){
        .fn_specs = expr_provider_fn_specs,
        .fn_spec_find = expr_provider_fn_spec_find,
        .source_specs = expr_provider_source_specs,
        .source_spec_find = expr_provider_source_spec_find,
        .expr_param_spec_for = expr_provider_expr_param_spec_for,
    },
};

static double host_provider_scalar(const char* name,
                                   const double* args,
                                   size_t argc,
                                   void* userdata) {
    (void)userdata;
    if (strcmp(name, "close") == 0 && argc == 0u) return 101.5;
    if (strcmp(name, "high") == 0 && argc == 0u) return 105.0;
    if (strcmp(name, "low") == 0 && argc == 0u) return 99.0;
    if (strcmp(name, "ema") == 0 && argc == 1u) return args[0] * 2.0;
    if (strcmp(name, "atr") == 0 && argc == 1u) return args[0] + 0.5;
    return NAN;
}

static void test_provider_host_runtime_supplies_expression_data(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_host_config host = {
        .runtime_required_scalar = host_provider_scalar,
        .userdata = NULL,
    };
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    double out = 0.0;

    assert(reg && ctx && parser);
    cxpr_register_provider_signatures(reg, &expr_provider, &host);
    ast = cxpr_expr_ast_parse(parser, "ema(10) + close() + atr(3)", &err);
    assert(ast != NULL);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(fabs(out - 125.0) < 1e-12);

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

static void test_provider_registration_helpers_are_directly_covered(void) {
    cxpr_registry* reg = cxpr_registry_new();
    const cxpr_provider_fn_spec* ema = cxpr_provider_fn_spec_find(&expr_provider, "ema");
    cxpr_expr_param_spec expr_spec = {0};
    size_t min_args = 0u;
    size_t max_args = 0u;
    assert(reg != NULL);
    assert(ema != NULL);

    cxpr_provider_host_visible_arg_range(ema, NULL, &min_args, &max_args);
    assert(min_args == 1u);
    /* source + period + optional `selector` scope arg, e.g.
       ema(close, period=10, selector="daily"). */
    assert(max_args == 3u);

    assert(cxpr_provider_expr_param_spec_for(&expr_provider, "ema", &expr_spec) != 0);
    assert(expr_spec.count == 2u);
    assert(expr_spec.kinds[0] == CXPR_EXPR_ARG_SCALAR_SOURCE);
    assert(expr_spec.kinds[1] == CXPR_EXPR_ARG_NUMERIC);

    assert(cxpr_register_provider_fn_spec(reg, ema, NULL) != 0);
    assert(cxpr_registry_lookup(reg, "ema", &min_args, &max_args) != 0);
    assert(min_args == 1u);
    assert(max_args == 3u);

    {
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_context* ctx = cxpr_context_new();
        cxpr_error err = {0};
        cxpr_expr_ast* ast;
        cxpr_expr_ast* bool_ast;
        double out = 0.0;

        assert(parser != NULL);
        assert(ctx != NULL);
        ast = cxpr_expr_ast_parse(parser, "ema(10)", &err);
        assert(ast != NULL);
        assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
        assert(isnan(out));
        cxpr_expr_ast_free(ast);

        err = (cxpr_error){0};
        bool_ast = cxpr_expr_ast_parse(parser, "ema(10) and true", &err);
        assert(bool_ast != NULL);
        assert(!cxpr_typecheck_bool_root(bool_ast, reg, &err));
        assert(err.code == CXPR_ERR_TYPE_MISMATCH);
        cxpr_expr_ast_free(bool_ast);

        cxpr_context_free(ctx);
        cxpr_expr_parser_free(parser);
    }

    cxpr_registry_free(reg);
}

static void test_runtime_call_helpers_are_directly_covered(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_runtime_call call = {0};
    const cxpr_expr_ast* source_arg;
    double values[1] = {0.0};

    assert(parser != NULL);
    assert(ctx != NULL);
    assert(reg != NULL);
    cxpr_register_defaults(reg);

    ast = cxpr_expr_ast_parse(parser, "ema(close, period=10, selector=\"daily\")", &err);
    assert(ast != NULL);
    assert(cxpr_parse_runtime_call(ast, &call) != 0);
    assert(call.kind == CXPR_RUNTIME_CALL_FUNCTION);
    assert(strcmp(call.name, "ema") == 0);

    memset(&call, 0, sizeof(call));
    assert(cxpr_parse_runtime_call_provider(&expr_provider, ast, &call) != 0);
    assert(call.scope_value != NULL && strcmp(call.scope_value, "daily") == 0);
    assert(call.value_arg_count == 1u);

    source_arg = cxpr_provider_runtime_call_arg(&expr_provider, ast, 0u);
    assert(source_arg != NULL);
    assert(cxpr_expr_ast_kind_of(source_arg) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_expr_ast_identifier_name(source_arg), "close") == 0);

    assert(cxpr_provider_eval_runtime_call_number_args(
        &expr_provider,
        ast,
        1u,
        ctx,
        reg,
        values,
        CXPR_ARRAY_COUNT(values),
        &err) != 0);
    assert(err.code == CXPR_OK);
    assert(values[0] == 10.0);

    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_parser_free(parser);
}

static void test_resolve_expression_scope(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_resolved_scope scope;

    assert(parser != NULL);

    ast = cxpr_expr_ast_parse(parser, "ema(close, period=10, selector=\"daily\") > close", &err);
    assert(ast != NULL);
    if (!cxpr_resolve_expression_scope(&expr_provider, ast, &scope)) abort();
    if (strcmp(scope.scope_name, "selector") != 0) abort();
    if (strcmp(scope.scope_value, "daily") != 0) abort();
    if (scope.origin == NULL) abort();
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser, "high(\"weekly\") > low", &err);
    assert(ast != NULL);
    if (!cxpr_resolve_expression_scope(&expr_provider, ast, &scope)) abort();
    if (strcmp(scope.scope_name, "selector") != 0) abort();
    if (strcmp(scope.scope_value, "weekly") != 0) abort();
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser, "foo(\"daily\")", &err);
    assert(ast != NULL);
    if (cxpr_resolve_expression_scope(&expr_provider, ast, &scope)) abort();
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(parser, "close > 10 ? \"daily\" : \"weekly\"", &err);
    assert(ast != NULL);
    if (cxpr_resolve_expression_scope(&expr_provider, ast, &scope)) abort();
    cxpr_expr_ast_free(ast);

    cxpr_expr_parser_free(parser);
}

static void test_source_plan_expression_binary_op(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    const cxpr_expr_ast* source_arg;
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "ema(atr(14) / close, 10)", &err);
    assert(ast != NULL);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL);

    source_arg = cxpr_expr_ast_call_arg(ast, 0);
    assert(source_arg != NULL);
    assert(cxpr_expr_ast_kind_of(source_arg) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, source_arg, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.root.expression_ast == source_arg);
    assert(plan.root.node_id != 0ULL);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "expr:") != NULL);
    assert(strstr(plan.canonical, "__div__") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_expression_via_smoothing(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "ema(atr(14) / close, 10)", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, ast, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_SMOOTHING);
    assert(strcmp(plan.root.name, "ema") == 0);
    assert(plan.root.source != NULL);
    assert(plan.root.source->kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.root.source->expression_ast != NULL);
    assert(plan.arg_count == 1u);

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_expression_with_lookback(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    const cxpr_expr_ast* source_arg;
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "ema(atr(14)[3] / close[3], 5)", &err);
    assert(ast != NULL);

    source_arg = cxpr_expr_ast_call_arg(ast, 0);
    assert(source_arg != NULL);
    assert(cxpr_expr_ast_kind_of(source_arg) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, source_arg, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "[3]") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_expression_simple_binary(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "high - low", &err);
    assert(ast != NULL);
    assert(cxpr_expr_ast_kind_of(ast) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, ast, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "high") != NULL);
    assert(strstr(plan.canonical, "__minus__") != NULL);
    assert(strstr(plan.canonical, "low") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_field_with_selector_and_lookback(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    double values[1] = {0.0};
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "close(\"abc\")[7]", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, ast, &plan);
    if (ok == 0) abort();
    if (plan.root.kind != CXPR_SOURCE_PLAN_FIELD) abort();
    if (strcmp(plan.root.name, "close") != 0) abort();
    if (plan.root.scope_value == NULL || strcmp(plan.root.scope_value, "abc") != 0) abort();
    if (plan.root.lookback_slot != 0u) abort();
    if (plan.arg_count != 1u) abort();
    if (plan.canonical == NULL) abort();
    if (strstr(plan.canonical, "field:close") == NULL) abort();
    if (strstr(plan.canonical, "@tf:abc") == NULL) abort();
    if (strstr(plan.canonical, "[$0]") == NULL) abort();
    if (cxpr_eval_source_plan_bound_args(&plan, NULL, NULL, values, 1u, &err) == 0) abort();
    if (err.code != CXPR_OK || values[0] != 7.0) abort();

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_smoothing_with_selector_and_lookback(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    double values[2] = {0.0, 0.0};
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "ema(close(\"abc\"), 10)[7]", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, ast, &plan);
    if (ok == 0) abort();
    if (plan.root.kind != CXPR_SOURCE_PLAN_SMOOTHING) abort();
    if (strcmp(plan.root.name, "ema") != 0) abort();
    if (plan.root.source == NULL) abort();
    if (plan.root.source->kind != CXPR_SOURCE_PLAN_FIELD) abort();
    if (plan.root.source->scope_value == NULL ||
        strcmp(plan.root.source->scope_value, "abc") != 0) {
        abort();
    }
    if (plan.root.lookback_slot != 1u) abort();
    if (plan.arg_count != 2u) abort();
    if (plan.root.arg_count != 1u || plan.root.arg_slots[0] != 0u) abort();
    if (plan.canonical == NULL) abort();
    if (strstr(plan.canonical, "smooth:ema(") == NULL) abort();
    if (strstr(plan.canonical, "field:close@tf:abc") == NULL) abort();
    if (strstr(plan.canonical, "[$1]") == NULL) abort();
    if (cxpr_eval_source_plan_bound_args(&plan, NULL, NULL, values, 2u, &err) == 0) abort();
    if (err.code != CXPR_OK || values[0] != 10.0 || values[1] != 7.0) abort();

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_source_plan_smoothing_with_named_source_arg(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_source_plan_ast plan;
    int ok;

    assert(parser != NULL);
    ast = cxpr_expr_ast_parse(parser, "ema(source=close, period=10)", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&expr_provider, ast, &plan);
    if (ok == 0) abort();
    if (plan.root.kind != CXPR_SOURCE_PLAN_SMOOTHING) abort();
    if (plan.root.arg_count != 1u || plan.root.arg_slots[0] != 0u) abort();
    if (plan.arg_count != 1u) abort();

    cxpr_free_source_plan_ast(&plan);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_provider_helpers_support_generic_series_scopes();
    printf("  ✓ cxpr provider generic scope helpers\n");
    test_provider_host_runtime_supplies_expression_data();
    printf("  ✓ cxpr provider host runtime data\n");
    test_provider_registration_helpers_are_directly_covered();
    printf("  ✓ cxpr provider registration helpers\n");
    test_runtime_call_helpers_are_directly_covered();
    printf("  ✓ cxpr runtime call helpers\n");
    test_provider_signatures_register_record_output_struct_producer();
    printf("  ✓ cxpr provider record-output struct producer registration\n");
    test_resolve_expression_scope();
    printf("  ✓ cxpr provider expression-level scope resolution\n");
    test_source_plan_expression_binary_op();
    printf("  ✓ source_plan: EXPRESSION from binary-op (atr(14) / close)\n");
    test_source_plan_expression_via_smoothing();
    printf("  ✓ source_plan: SMOOTHING with EXPRESSION child (ema(atr(14)/close, 10))\n");
    test_source_plan_expression_with_lookback();
    printf("  ✓ source_plan: EXPRESSION with lookback (atr(14)[3] / close[3])\n");
    test_source_plan_expression_simple_binary();
    printf("  ✓ source_plan: EXPRESSION simple binary (high - low)\n");
    test_source_plan_field_with_selector_and_lookback();
    printf("  ✓ source_plan: FIELD with selector and lookback (close(\"abc\")[7])\n");
    test_source_plan_smoothing_with_selector_and_lookback();
    printf("  ✓ source_plan: SMOOTHING with selector and lookback (ema(close(\"abc\"), 10)[7])\n");
    test_source_plan_smoothing_with_named_source_arg();
    printf("  ✓ source_plan: SMOOTHING with named source arg\n");
    return 0;
}
