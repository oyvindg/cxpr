#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cxpr/provider.h>

#define CXPR_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static const cxpr_provider_fn_spec* const* test_provider_fn_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_param_descriptor kConsumerParams[] = {
        {"period"},
    };
    static const cxpr_provider_scope_spec kScope = {
        "resolution",
        1,
    };
    static const cxpr_provider_fn_spec kConsumer = {
        .name = "moving_average",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 1u,
        .source_max_args = 1u,
        .params = kConsumerParams,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
        .scope = &kScope,
    };
    static const cxpr_provider_fn_spec* const kSpecs[] = {
        &kConsumer,
    };

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(kSpecs);
    return kSpecs;
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
    static const cxpr_provider_scope_spec kScope = {
        "resolution",
        1,
    };
    static const cxpr_provider_source_spec kSource = {
        "temperature",
        0u,
        1u,
        &kScope,
    };
    static const cxpr_provider_source_spec* const kSpecs[] = {
        &kSource,
    };

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(kSpecs);
    return kSpecs;
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
    static const cxpr_provider_vtable kVtable = {
        .fn_specs = test_provider_fn_specs,
        .fn_spec_find = test_provider_fn_spec_find,
        .source_specs = test_provider_source_specs,
        .source_spec_find = test_provider_source_spec_find,
        .expr_param_spec_for = NULL,
    };
    static const cxpr_provider kProvider = {
        "generic-metrics",
        NULL,
        &kVtable,
    };
    size_t fn_count = 0u;
    size_t source_count = 0u;
    const cxpr_provider_fn_spec* fn;
    const cxpr_provider_source_spec* source;

    if (cxpr_provider_is_valid(&kProvider) == 0) abort();
    if (cxpr_provider_fn_specs(&kProvider, &fn_count) == NULL || fn_count != 1u) abort();
    if (cxpr_provider_source_specs(&kProvider, &source_count) == NULL ||
        source_count != 1u) {
        abort();
    }

    fn = cxpr_provider_fn_spec_find(&kProvider, "moving_average");
    if (!fn || !fn->scope || strcmp(fn->scope->param_name, "resolution") != 0) abort();

    source = cxpr_provider_source_spec_find(&kProvider, "temperature");
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
    static const cxpr_provider_param_descriptor kParams[] = {
        {"fast"},
        {"slow"},
    };
    static const cxpr_provider_field_descriptor kFields[] = {
        {"value"},
        {"signal"},
    };
    static const cxpr_provider_fn_spec kRecordFn = {
        .name = "record_fn",
        .min_args = 1u,
        .max_args = 2u,
        .params = kParams,
        .param_count = CXPR_ARRAY_COUNT(kParams),
        .fields = kFields,
        .field_count = CXPR_ARRAY_COUNT(kFields),
        .primary_field_index = 0,
        .flags = CXPR_PROVIDER_FN_RECORD_OUTPUT,
    };
    static const cxpr_provider_fn_spec* const kSpecs[] = {
        &kRecordFn,
    };
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(kSpecs);
    return kSpecs;
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
    static const cxpr_provider_vtable kVtable = {
        .fn_specs = test_record_provider_fn_specs,
        .fn_spec_find = test_record_provider_fn_spec_find,
        .source_specs = test_provider_source_specs,
        .source_spec_find = test_provider_source_spec_find,
        .expr_param_spec_for = NULL,
    };
    static const cxpr_provider kProvider = {
        "record-test",
        NULL,
        &kVtable,
    };
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;
    cxpr_value value = {0};

    assert(reg != NULL);
    assert(parser != NULL);
    assert(ctx != NULL);

    cxpr_register_provider_signatures(
        reg,
        &kProvider,
        &(const cxpr_host_config){
            .runtime_required_scalar = test_runtime_required_scalar,
        });

    ast = cxpr_parse(parser, "record_fn(3, 5).signal", &err);
    if (ast == NULL) abort();
    prog = cxpr_compile(ast, reg, &err);
    if (prog == NULL) abort();
    if (!cxpr_eval_program(prog, ctx, reg, &value, &err)) abort();
    if (err.code != CXPR_OK || value.type != CXPR_VALUE_NUMBER || value.d != 25.0) abort();

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    cxpr_registry_free(reg);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Source-plan EXPRESSION node test fixtures
 * ═══════════════════════════════════════════════════════════════════════════ */

static const cxpr_provider_scope_spec kExprScope = {
    "selector",
    1,
};

static const cxpr_provider_fn_spec* const* expr_provider_fn_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_param_descriptor kEmaParams[] = {{"period"}};
    static const cxpr_provider_param_descriptor kAtrParams[] = {{"period"}};
    static const cxpr_provider_fn_spec kEma = {
        .name = "ema",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 1u,
        .source_max_args = 1u,
        .params = kEmaParams,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
        .scope = &kExprScope,
    };
    static const cxpr_provider_fn_spec kAtr = {
        .name = "atr",
        .min_args = 1u,
        .max_args = 1u,
        .source_min_args = 0u,
        .source_max_args = 0u,
        .params = kAtrParams,
        .param_count = 1u,
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = 0u,
        .scope = NULL,
    };
    static const cxpr_provider_fn_spec* const kSpecs[] = {&kEma, &kAtr};

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(kSpecs);
    return kSpecs;
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
    static const cxpr_provider_source_spec kClose = {"close", 0u, 1u, &kExprScope};
    static const cxpr_provider_source_spec kHigh  = {"high",  0u, 1u, &kExprScope};
    static const cxpr_provider_source_spec kLow   = {"low",   0u, 1u, &kExprScope};
    static const cxpr_provider_source_spec* const kSpecs[] = {&kClose, &kHigh, &kLow};
    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(kSpecs);
    return kSpecs;
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
    static const char* const kEmaNames[] = {"source", "period"};
    static const cxpr_expr_arg_kind kEmaKinds[] = {
        CXPR_EXPR_ARG_SCALAR_SOURCE,
        CXPR_EXPR_ARG_NUMERIC,
    };
    (void)userdata;
    if (!name || !out || strcmp(name, "ema") != 0) return 0;
    *out = (cxpr_expr_param_spec){
        .names = kEmaNames,
        .defaults = NULL,
        .kinds = kEmaKinds,
        .count = CXPR_ARRAY_COUNT(kEmaNames),
        .min_count = 2u,
        .lookback_sugar_name = NULL,
        .has_timeframe_param = 0,
    };
    return 1;
}

static const cxpr_provider kExprProvider = {
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

static void test_provider_registration_helpers_are_directly_covered(void) {
    cxpr_registry* reg = cxpr_registry_new();
    const cxpr_provider_fn_spec* ema = cxpr_provider_fn_spec_find(&kExprProvider, "ema");
    cxpr_expr_param_spec expr_spec = {0};
    size_t min_args = 0u;
    size_t max_args = 0u;
    assert(reg != NULL);
    assert(ema != NULL);

    cxpr_provider_host_visible_arg_range(ema, NULL, &min_args, &max_args);
    assert(min_args == 1u);
    assert(max_args == 2u);

    assert(cxpr_provider_expr_param_spec_for(&kExprProvider, "ema", &expr_spec) != 0);
    assert(expr_spec.count == 2u);
    assert(expr_spec.kinds[0] == CXPR_EXPR_ARG_SCALAR_SOURCE);
    assert(expr_spec.kinds[1] == CXPR_EXPR_ARG_NUMERIC);

    assert(cxpr_register_provider_fn_spec(reg, ema, NULL) != 0);
    assert(cxpr_registry_lookup(reg, "ema", &min_args, &max_args) != 0);
    assert(min_args == 1u);
    assert(max_args == 2u);

    cxpr_registry_free(reg);
}

static void test_runtime_call_helpers_are_directly_covered(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_runtime_call call = {0};
    const cxpr_ast* source_arg;
    double values[1] = {0.0};

    assert(parser != NULL);
    assert(ctx != NULL);
    assert(reg != NULL);
    cxpr_register_defaults(reg);

    ast = cxpr_parse(parser, "ema(close, period=10, selector=\"daily\")", &err);
    assert(ast != NULL);
    assert(cxpr_parse_runtime_call(ast, &call) != 0);
    assert(call.kind == CXPR_RUNTIME_CALL_FUNCTION);
    assert(strcmp(call.name, "ema") == 0);

    memset(&call, 0, sizeof(call));
    assert(cxpr_parse_runtime_call_provider(&kExprProvider, ast, &call) != 0);
    assert(call.scope_value != NULL && strcmp(call.scope_value, "daily") == 0);
    assert(call.value_arg_count == 1u);

    source_arg = cxpr_provider_runtime_call_arg(&kExprProvider, ast, 0u);
    assert(source_arg != NULL);
    assert(cxpr_ast_type(source_arg) == CXPR_NODE_IDENTIFIER);
    assert(strcmp(cxpr_ast_identifier_name(source_arg), "close") == 0);

    assert(cxpr_provider_eval_runtime_call_number_args(
        &kExprProvider,
        ast,
        1u,
        ctx,
        reg,
        values,
        CXPR_ARRAY_COUNT(values),
        &err) != 0);
    assert(err.code == CXPR_OK);
    assert(values[0] == 10.0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_resolve_expression_scope(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_resolved_scope scope;

    assert(parser != NULL);

    ast = cxpr_parse(parser, "ema(close, period=10, selector=\"daily\") > close", &err);
    assert(ast != NULL);
    if (!cxpr_resolve_expression_scope(&kExprProvider, ast, &scope)) abort();
    if (strcmp(scope.scope_name, "selector") != 0) abort();
    if (strcmp(scope.scope_value, "daily") != 0) abort();
    if (scope.origin == NULL) abort();
    cxpr_ast_free(ast);

    ast = cxpr_parse(parser, "high(\"weekly\") > low", &err);
    assert(ast != NULL);
    if (!cxpr_resolve_expression_scope(&kExprProvider, ast, &scope)) abort();
    if (strcmp(scope.scope_name, "selector") != 0) abort();
    if (strcmp(scope.scope_value, "weekly") != 0) abort();
    cxpr_ast_free(ast);

    ast = cxpr_parse(parser, "foo(\"daily\")", &err);
    assert(ast != NULL);
    if (cxpr_resolve_expression_scope(&kExprProvider, ast, &scope)) abort();
    cxpr_ast_free(ast);

    ast = cxpr_parse(parser, "close > 10 ? \"daily\" : \"weekly\"", &err);
    assert(ast != NULL);
    if (cxpr_resolve_expression_scope(&kExprProvider, ast, &scope)) abort();
    cxpr_ast_free(ast);

    cxpr_parser_free(parser);
}

static void test_source_plan_expression_binary_op(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    const cxpr_ast* source_arg;
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "ema(atr(14) / close, 10)", &err);
    assert(ast != NULL);
    assert(cxpr_ast_type(ast) == CXPR_NODE_FUNCTION_CALL);

    source_arg = cxpr_ast_function_arg(ast, 0);
    assert(source_arg != NULL);
    assert(cxpr_ast_type(source_arg) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, source_arg, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.root.expression_ast == source_arg);
    assert(plan.root.node_id != 0ULL);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "expr:") != NULL);
    assert(strstr(plan.canonical, "__div__") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_source_plan_expression_via_smoothing(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "ema(atr(14) / close, 10)", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, ast, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_SMOOTHING);
    assert(strcmp(plan.root.name, "ema") == 0);
    assert(plan.root.source != NULL);
    assert(plan.root.source->kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.root.source->expression_ast != NULL);
    assert(plan.arg_count == 1u);

    cxpr_free_source_plan_ast(&plan);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_source_plan_expression_with_lookback(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    const cxpr_ast* source_arg;
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "ema(atr(14)[3] / close[3], 5)", &err);
    assert(ast != NULL);

    source_arg = cxpr_ast_function_arg(ast, 0);
    assert(source_arg != NULL);
    assert(cxpr_ast_type(source_arg) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, source_arg, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "[3]") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_source_plan_expression_simple_binary(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "high - low", &err);
    assert(ast != NULL);
    assert(cxpr_ast_type(ast) == CXPR_NODE_BINARY_OP);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, ast, &plan);
    if (ok == 0) abort();
    assert(plan.root.kind == CXPR_SOURCE_PLAN_EXPRESSION);
    assert(plan.canonical != NULL);
    assert(strstr(plan.canonical, "high") != NULL);
    assert(strstr(plan.canonical, "__minus__") != NULL);
    assert(strstr(plan.canonical, "low") != NULL);

    cxpr_free_source_plan_ast(&plan);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_source_plan_field_with_selector_and_lookback(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    double values[1] = {0.0};
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "close(\"abc\")[7]", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, ast, &plan);
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
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_source_plan_smoothing_with_selector_and_lookback(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_source_plan_ast plan;
    double values[2] = {0.0, 0.0};
    int ok;

    assert(parser != NULL);
    ast = cxpr_parse(parser, "ema(close(\"abc\"), 10)[7]", &err);
    assert(ast != NULL);

    memset(&plan, 0, sizeof(plan));
    ok = cxpr_parse_provider_source_plan_ast(&kExprProvider, ast, &plan);
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
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

int main(void) {
    test_provider_helpers_support_generic_series_scopes();
    printf("  ✓ cxpr provider generic scope helpers\n");
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
    return 0;
}
