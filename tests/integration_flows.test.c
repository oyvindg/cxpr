#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/cxpr.h>

typedef struct {
    size_t calls;
} host_state;

static const cxpr_provider_scope_spec flow_scope = {
    "selector",
    1,
};

static const cxpr_provider_fn_spec* const* flow_provider_fn_specs(
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
        .param_count = CXPR_ARRAY_COUNT(ema_params),
        .fields = NULL,
        .field_count = 0u,
        .primary_field_index = -1,
        .flags = CXPR_PROVIDER_FN_SOURCE_INPUT,
        .scope = &flow_scope,
    };
    static const cxpr_provider_fn_spec atr = {
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
        .scope = NULL,
    };
    static const cxpr_provider_fn_spec* const specs[] = {&ema, &atr};

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_fn_spec* flow_provider_fn_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_fn_spec* const* specs = flow_provider_fn_specs(userdata, &count);

    (void)userdata;
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static const cxpr_provider_source_spec* const* flow_provider_source_specs(
    const void* userdata,
    size_t* count) {
    static const cxpr_provider_source_spec close = {"close", 0u, 1u, &flow_scope};
    static const cxpr_provider_source_spec high = {"high", 0u, 1u, &flow_scope};
    static const cxpr_provider_source_spec low = {"low", 0u, 1u, &flow_scope};
    static const cxpr_provider_source_spec* const specs[] = {&close, &high, &low};

    (void)userdata;
    if (count) *count = CXPR_ARRAY_COUNT(specs);
    return specs;
}

static const cxpr_provider_source_spec* flow_provider_source_spec_find(
    const void* userdata,
    const char* name) {
    size_t count = 0u;
    const cxpr_provider_source_spec* const* specs =
        flow_provider_source_specs(userdata, &count);

    (void)userdata;
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(specs[i]->name, name) == 0) return specs[i];
    }
    return NULL;
}

static const cxpr_provider flow_provider = {
    "integration-flow",
    NULL,
    &(const cxpr_provider_vtable){
        .fn_specs = flow_provider_fn_specs,
        .fn_spec_find = flow_provider_fn_spec_find,
        .source_specs = flow_provider_source_specs,
        .source_spec_find = flow_provider_source_spec_find,
        .expr_param_spec_for = NULL,
    },
};

static double flow_host_scalar(const char* name,
                               const double* args,
                               size_t argc,
                               void* userdata) {
    host_state* state = (host_state*)userdata;
    if (state) state->calls += 1u;

    if (strcmp(name, "close") == 0 && argc == 0u) return 101.0;
    if (strcmp(name, "high") == 0 && argc == 0u) return 108.0;
    if (strcmp(name, "low") == 0 && argc == 0u) return 100.0;
    if (strcmp(name, "ema") == 0 && argc == 1u) return args[0] + 100.0;
    if (strcmp(name, "atr") == 0 && argc == 1u) return args[0] + 4.0;
    return NAN;
}

static void test_provider_evaluator_strategy_flow(void) {
    host_state state = {0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_evaluator* evaluator;
    cxpr_error err = {0};
    cxpr_host_config host = {
        .runtime_required_scalar = flow_host_scalar,
        .userdata = &state,
    };
    const cxpr_expression_def defs[] = {
        {"spread", "high() - low()"},
        {"trend", "ema(10) > close()"},
        {"entry", "trend && spread < atr(5)"},
    };
    bool found = false;

    assert(reg != NULL);
    assert(ctx != NULL);
    cxpr_register_provider_signatures(reg, &flow_provider, &host);

    evaluator = cxpr_evaluator_new(reg);
    assert(evaluator != NULL);
    assert(cxpr_expressions_add(evaluator, defs, CXPR_ARRAY_COUNT(defs), &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    assert(cxpr_expression_get_double(evaluator, "spread", &found) == 8.0 && found);
    assert(cxpr_expression_get_bool(evaluator, "trend", &found) && found);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) && found);
    assert(state.calls >= 5u);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

static void test_context_overlay_parse_compile_eval_flow(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* overlay;
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* program;
    double out = 0.0;
    bool found = false;

    assert(reg != NULL);
    assert(parent != NULL);
    assert(parser != NULL);
    cxpr_register_defaults(reg);

    cxpr_context_set(parent, "close", 100.0);
    cxpr_context_set(parent, "high", 110.0);
    cxpr_context_set(parent, "fee", 1.0);

    overlay = cxpr_context_overlay_new(parent);
    assert(overlay != NULL);
    cxpr_context_set(overlay, "close", 105.0);

    ast = cxpr_parse(parser, "close + high - fee", &err);
    assert(ast != NULL);
    program = cxpr_compile(ast, reg, &err);
    assert(program != NULL);
    assert(cxpr_eval_program_number(program, overlay, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out == 214.0);
    assert(cxpr_context_get(parent, "close", &found) == 100.0 && found);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(overlay);
    cxpr_context_free(parent);
    cxpr_registry_free(reg);
}

static void test_defined_function_overlay_flow(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* parent = cxpr_context_new();
    cxpr_context* overlay;
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* program;
    bool out = false;

    assert(reg != NULL);
    assert(parent != NULL);
    assert(parser != NULL);
    cxpr_register_defaults(reg);
    assert(cxpr_registry_define_fn(reg, "net(price) => price - fee").code == CXPR_OK);

    cxpr_context_set(parent, "close", 100.0);
    cxpr_context_set(parent, "fee", 1.0);
    overlay = cxpr_context_overlay_new(parent);
    assert(overlay != NULL);
    cxpr_context_set(overlay, "fee", 2.0);

    ast = cxpr_parse(parser, "net(close) == 98", &err);
    assert(ast != NULL);
    program = cxpr_compile(ast, reg, &err);
    assert(program != NULL);
    assert(cxpr_eval_program_bool(program, overlay, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(overlay);
    cxpr_context_free(parent);
    cxpr_registry_free(reg);
}

static void test_defined_function_struct_field_flow(void) {
    static const char* fields[] = {"x", "y"};
    static const double values[] = {12.0, 4.0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* program;
    double out = 0.0;

    assert(reg != NULL);
    assert(ctx != NULL);
    assert(parser != NULL);
    cxpr_register_defaults(reg);
    assert(cxpr_registry_define_fn(reg, "pick_x(point) => point.x").code == CXPR_OK);
    cxpr_context_set_fields(ctx, "pose", fields, values, CXPR_ARRAY_COUNT(fields));

    ast = cxpr_parse(parser, "pick_x(pose) + pose.y", &err);
    assert(ast != NULL);
    program = cxpr_compile(ast, reg, &err);
    assert(program != NULL);
    assert(cxpr_eval_program_number(program, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out == 16.0);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

static double base_scale(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1u);
    return args[0] * 2.0;
}

static cxpr_value timeframe_scale_handler(const cxpr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    const cxpr_ast* value = cxpr_ast_function_arg(call_ast, 0);
    const cxpr_ast* timeframe = cxpr_ast_function_arg(call_ast, 1);
    double out = 0.0;

    (void)userdata;
    if (!value || !cxpr_eval_ast_number(value, ctx, reg, &out, err)) {
        return cxpr_num(NAN);
    }
    if (timeframe && cxpr_ast_type(timeframe) == CXPR_NODE_STRING &&
        strcmp(cxpr_ast_string_value(timeframe), "daily") == 0) {
        return cxpr_num(out + 1000.0);
    }
    return cxpr_num(out * 2.0);
}

static void test_ast_handler_parse_compile_eval_flow(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* program;
    double out = 0.0;

    assert(reg != NULL);
    assert(ctx != NULL);
    assert(parser != NULL);
    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "scaled", base_scale, 1u, 1u, NULL, NULL);
    cxpr_registry_add_ast_handler(
        reg, "scaled", timeframe_scale_handler, 1u, 2u, NULL, NULL);
    cxpr_context_set(ctx, "close", 7.0);

    ast = cxpr_parse(parser, "scaled(close, \"daily\") + scaled(3)", &err);
    assert(ast != NULL);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out == 1013.0);

    program = cxpr_compile(ast, reg, &err);
    assert(program != NULL);
    assert(cxpr_eval_program_number(program, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out == 1013.0);

    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
}

int main(void) {
    test_provider_evaluator_strategy_flow();
    test_context_overlay_parse_compile_eval_flow();
    test_defined_function_overlay_flow();
    test_defined_function_struct_field_flow();
    test_ast_handler_parse_compile_eval_flow();
    printf("  ok integration_flows\n");
    return 0;
}
