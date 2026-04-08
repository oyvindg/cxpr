#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

typedef struct {
    const double* close;
    const double* base;
    size_t length;
    size_t current_index;
} test_series_env;

static cxpr_ast* parse_or_die(cxpr_parser* parser, const char* expr) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n", expr, err.message ? err.message : "(null)");
        assert(0);
    }
    return ast;
}

static bool test_lookup_series_value(const test_series_env* env,
                                     const char* name,
                                     size_t index,
                                     double* out) {
    if (!env || !name || !out || index >= env->length) return false;

    if (strcmp(name, "close") == 0) {
        *out = env->close[index];
        return true;
    }
    if (strcmp(name, "base") == 0) {
        *out = env->base[index];
        return true;
    }

    return false;
}

static bool test_series_lookback_resolver(const cxpr_ast* target,
                                          const cxpr_ast* index_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_value* out_value,
                                          cxpr_error* err) {
    const test_series_env* env = (const test_series_env*)userdata;
    double offset_value = 0.0;
    long long offset = 0;
    size_t shifted_index = 0;
    cxpr_context* shifted = NULL;
    double scalar = 0.0;

    if (!env) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Missing test series environment";
        }
        return true;
    }

    if (!cxpr_eval_ast_number(index_ast, ctx, reg, &offset_value, err)) {
        return true;
    }
    offset = (long long)llround(offset_value);
    if (!isfinite(offset_value) || fabs(offset_value - (double)offset) > 1e-9 || offset < 0) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback offset must be a non-negative integer";
        }
        return true;
    }
    if ((size_t)offset > env->current_index) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Lookback offset exceeds available history";
        }
        return true;
    }

    shifted_index = env->current_index - (size_t)offset;
    shifted = cxpr_context_clone(ctx);
    assert(shifted != NULL);

    if (test_lookup_series_value(env, "close", shifted_index, &scalar)) {
        cxpr_context_set(shifted, "close", scalar);
    }
    if (test_lookup_series_value(env, "base", shifted_index, &scalar)) {
        cxpr_context_set(shifted, "base", scalar);
    }

    if (!cxpr_eval_ast(target, shifted, reg, out_value, err)) {
        cxpr_context_free(shifted);
        return true;
    }
    cxpr_context_free(shifted);

    if (out_value->type == CXPR_VALUE_NUMBER && isnan(out_value->d)) {
        return true;
    }

    return true;
}

static cxpr_value strictly_rising_fn(const cxpr_ast* call_ast,
                                     const cxpr_context* ctx,
                                     const cxpr_registry* reg,
                                     void* userdata,
                                     cxpr_error* err) {
    (void)userdata;
    assert(call_ast != NULL);
    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "strictly_rising") == 0);
    assert(cxpr_ast_function_argc(call_ast) == 2);

    const cxpr_ast* value_ast = cxpr_ast_function_arg(call_ast, 0);
    const cxpr_ast* bars_ast = cxpr_ast_function_arg(call_ast, 1);
    double bars_value = 0.0;
    long long bars = 0;

    if (!cxpr_eval_ast_number(bars_ast, ctx, reg, &bars_value, err)) {
        return cxpr_fv_bool(false);
    }

    bars = (long long)llround(bars_value);
    if (!isfinite(bars_value) || fabs(bars_value - (double)bars) > 1e-9 || bars < 2) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "strictly_rising(...) requires integer bars >= 2";
        }
        return cxpr_fv_bool(false);
    }

    for (long long i = 0; i < bars - 1; ++i) {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &lhs, err) ||
            !cxpr_eval_ast_number_at_offset(value_ast, (double)(i + 1), ctx, reg, &rhs, err)) {
            return cxpr_fv_bool(false);
        }
        if (!(lhs > rhs)) return cxpr_fv_bool(false);
    }

    return cxpr_fv_bool(true);
}

static void test_eval_ast_at_offset_reuses_lookback_resolver(void) {
    static const double close_series[] = {10.0, 11.0, 12.0, 13.0, 14.0};
    static const double base_series[] = {2.0, 3.0, 4.0, 5.0, 6.0};
    const test_series_env env = {
        .close = close_series,
        .base = base_series,
        .length = 5,
        .current_index = 4,
    };

    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;
    bool bool_out = false;

    cxpr_register_builtins(reg);
    cxpr_registry_set_lookback_resolver(reg, test_series_lookback_resolver, (void*)&env, NULL);
    cxpr_context_set(ctx, "close", close_series[env.current_index]);
    cxpr_context_set(ctx, "base", base_series[env.current_index]);

    cxpr_ast* ast = parse_or_die(parser, "close + base");
    assert(cxpr_eval_ast_number_at_offset(ast, 2.0, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 16.0);

    cxpr_ast_free(ast);
    ast = parse_or_die(parser, "close > base");
    assert(cxpr_eval_ast_bool_at_offset(ast, 1.0, ctx, reg, &bool_out, &err));
    assert(err.code == CXPR_OK);
    assert(bool_out);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_eval_ast_at_offset_reuses_lookback_resolver\n");
}

static void test_builtin_rising_and_falling_use_native_timeseries_eval(void) {
    static const double rising_close[] = {9.0, 10.0, 11.0, 12.0, 13.0};
    static const double rising_base[] = {0.0, 0.0, 0.0, 0.0, 0.0};
    static const double falling_close[] = {13.0, 12.0, 11.0, 10.0, 9.0};
    static const double falling_base[] = {0.0, 0.0, 0.0, 0.0, 0.0};
    static const double mixed_close[] = {13.0, 12.0, 11.0, 12.0, 13.0};
    static const double mixed_base[] = {0.0, 0.0, 0.0, 0.0, 0.0};
    test_series_env env = {
        .close = rising_close,
        .base = rising_base,
        .length = 5,
        .current_index = 4,
    };

    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    bool out = false;

    cxpr_register_builtins(reg);
    cxpr_registry_set_lookback_resolver(reg, test_series_lookback_resolver, &env, NULL);
    cxpr_context_set(ctx, "close", rising_close[env.current_index]);
    cxpr_context_set(ctx, "base", rising_base[env.current_index]);

    cxpr_ast* ast = parse_or_die(parser, "rising(close, 3)");
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out);
    cxpr_ast_free(ast);

    ast = parse_or_die(parser, "falling(close, 3)");
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(!out);
    cxpr_ast_free(ast);

    env.close = falling_close;
    env.base = falling_base;
    cxpr_context_set(ctx, "close", falling_close[env.current_index]);
    cxpr_context_set(ctx, "base", falling_base[env.current_index]);

    ast = parse_or_die(parser, "falling(close, 3)");
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out);
    cxpr_ast_free(ast);

    env.close = mixed_close;
    env.base = mixed_base;
    cxpr_context_set(ctx, "close", mixed_close[env.current_index]);
    cxpr_context_set(ctx, "base", mixed_base[env.current_index]);

    ast = parse_or_die(parser, "falling(close, 3)");
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(!out);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_builtin_rising_and_falling_use_native_timeseries_eval\n");
}

static void test_registered_timeseries_function_uses_same_api(void) {
    static const double close_series[] = {10.0, 11.0, 12.0, 13.0, 14.0};
    static const double base_series[] = {1.0, 1.0, 1.0, 1.0, 1.0};
    const test_series_env env = {
        .close = close_series,
        .base = base_series,
        .length = 5,
        .current_index = 4,
    };

    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    bool out = false;

    cxpr_register_builtins(reg);
    cxpr_registry_set_lookback_resolver(reg, test_series_lookback_resolver, (void*)&env, NULL);
    cxpr_registry_add_timeseries(reg, "strictly_rising", strictly_rising_fn, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);

    cxpr_context_set(ctx, "close", close_series[env.current_index]);
    cxpr_context_set(ctx, "base", base_series[env.current_index]);

    cxpr_ast* ast = parse_or_die(parser, "strictly_rising(close + base, 3)");
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    assert(out);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
    printf("  \xE2\x9C\x93 test_registered_timeseries_function_uses_same_api\n");
}

int main(void) {
    printf("Running timeseries tests...\n");
    test_eval_ast_at_offset_reuses_lookback_resolver();
    test_builtin_rising_and_falling_use_native_timeseries_eval();
    test_registered_timeseries_function_uses_same_api();
    printf("All timeseries tests passed!\n");
    return 0;
}
