#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const double (*prices)[3];
    size_t calls;
} test_price_env;

typedef struct {
    const double* values;
    size_t count;
    const int64_t* index_map;
    size_t map_count;
} mapped_view_env;

static bool array_view(int64_t index,
                       const char* name,
                       const double* args,
                       size_t argc,
                       double* out,
                       void* userdata) {
    const double* values = (const double*)userdata;
    (void)name;
    (void)args;
    (void)argc;
    if (!values || !out || index < 0 || index >= 5) return false;
    *out = values[index];
    return true;
}

static bool pair_price_view(int64_t index,
                            const char* name,
                            const double* args,
                            size_t argc,
                            double* out,
                            void* userdata) {
    test_price_env* env = (test_price_env*)userdata;
    int pair;
    (void)name;
    if (!env || !env->prices || !out || index < 0 || index >= 5 || argc != 1u) return false;
    env->calls++;
    pair = (int)args[0];
    if (pair < 1 || pair > 3) return false;
    *out = env->prices[index][pair - 1];
    return true;
}

static bool mapped_array_view(int64_t index,
                              const char* name,
                              const double* args,
                              size_t argc,
                              double* out,
                              void* userdata) {
    const mapped_view_env* env = (const mapped_view_env*)userdata;
    (void)name;
    (void)args;
    (void)argc;
    if (!env || !env->values || !out || index < 0 || (size_t)index >= env->count) return false;
    *out = env->values[index];
    return true;
}

static int64_t mapped_array_index(int64_t cursor, void* userdata) {
    const mapped_view_env* env = (const mapped_view_env*)userdata;
    if (!env || !env->index_map || cursor < 0 || (size_t)cursor >= env->map_count) return -1;
    return env->index_map[cursor];
}

static double pull_with_args(const char* name, const double* args, size_t argc, void* userdata) {
    size_t* calls = (size_t*)userdata;
    (void)name;
    if (calls && argc > 0u) (*calls)++;
    return argc > 0u ? args[0] * 10.0 + (calls ? (double)(*calls) : 0.0) : 0.0;
}

static cxpr_value band_value_fn(const cxpr_value* args, size_t argc, void* userdata) {
    const char* fields[] = {"lower", "upper"};
    cxpr_value values[2];
    cxpr_struct_value* sv;
    double close;
    (void)userdata;
    assert(argc == 1u);
    assert(args[0].type == CXPR_VALUE_NUMBER);
    close = args[0].d;
    values[0] = cxpr_num(close - 1.0);
    values[1] = cxpr_num(close + 1.0);
    sv = cxpr_struct_value_new(fields, values, 2u);
    assert(sv != NULL);
    return cxpr_struct(sv);
}

static cxpr_value nested_box_value_fn(const cxpr_value* args, size_t argc, void* userdata) {
    const char* inner_fields[] = {"value"};
    const char* outer_fields[] = {"inner"};
    cxpr_value inner_values[1];
    cxpr_value outer_values[1];
    cxpr_struct_value* inner;
    cxpr_struct_value* outer;
    double close;
    (void)userdata;
    assert(argc == 1u);
    assert(args[0].type == CXPR_VALUE_NUMBER);
    close = args[0].d;
    inner_values[0] = cxpr_num(close + 0.5);
    inner = cxpr_struct_value_new(inner_fields, inner_values, 1u);
    assert(inner != NULL);
    outer_values[0] = cxpr_struct(inner);
    outer = cxpr_struct_value_new(outer_fields, outer_values, 1u);
    assert(outer != NULL);
    cxpr_struct_value_free(inner);
    return cxpr_struct(outer);
}

static cxpr_value shifted_ast_fn(const cxpr_expr_ast* call_ast,
                                 const cxpr_context* ctx,
                                 const cxpr_registry* reg,
                                 void* userdata,
                                 cxpr_error* err) {
    double arg = NAN;
    size_t offset = 0u;
    (void)userdata;
    assert(cxpr_expr_ast_call_arg_count(call_ast) == 1u);
    if (!cxpr_eval_ast_number(cxpr_expr_ast_call_arg(call_ast, 0u), ctx, reg, &arg, err)) {
        return cxpr_num(NAN);
    }
    return cxpr_num(
        arg + (cxpr_engine_context_lookback_offset(ctx, &offset) ? (double)offset * 100.0 : 0.0));
}

static bool inline_shifted_policy(const cxpr_expr_ast* target, void* userdata) {
    size_t* calls = (size_t*)userdata;
    if (calls) (*calls)++;
    return target &&
           cxpr_expr_ast_kind_of(target) == CXPR_NODE_FUNCTION_CALL &&
           strcmp(cxpr_expr_ast_call_name(target), "shifted") == 0;
}

static bool host_counting_resolver(const cxpr_expr_ast* target, const cxpr_expr_ast* index,
                                   const cxpr_context* ctx, const cxpr_registry* reg,
                                   void* userdata, cxpr_value* out, cxpr_error* err) {
    size_t* calls = (size_t*)userdata;
    (void)target;
    (void)index;
    (void)ctx;
    (void)reg;
    (void)err;
    if (calls) (*calls)++;
    if (out) *out = cxpr_num(-999.0);
    return true;
}

static void test_engine_view_source_lookback(void) {
    static const double values[5] = {2.0, 4.0, 8.0, 16.0, 32.0};
    const cxpr_expression_def exprs[] = {
        {"v0", "view_px"},
        {"v1", "view_px[1]"},
        {"v2", "view_px[2]"},
    };
    const cxpr_engine_view_source_def views[] = {
        {"view_px", array_view, (void*)values},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double v1[5];
    double v2[5];
    size_t i;

    cfg.expressions = exprs;
    cfg.expression_count = 3;
    cfg.view_sources = views;
    cfg.view_source_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        v1[i] = cxpr_engine_get_double(session, "v1", &found);
        assert(found);
        v2[i] = cxpr_engine_get_double(session, "v2", &found);
        assert(found);
    }
    assert(isnan(v1[0]));
    assert(v1[3] == 8.0);
    assert(v2[4] == 8.0);
    cxpr_engine_session_free(session);
}

static void test_engine_view_source_map_index_before_read(void) {
    static const double secondary_values[3] = {100.0, 200.0, 300.0};
    static const int64_t index_map[5] = {-1, 0, 0, 1, 2};
    mapped_view_env env = {
        secondary_values,
        3u,
        index_map,
        5u,
    };
    const cxpr_expression_def exprs[] = {
        {"mapped_now", "secondary"},
        {"mapped_prev", "secondary[1]"},
    };
    const cxpr_engine_view_source_def views[] = {
        {"secondary", mapped_array_view, &env, mapped_array_index},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double now[5];
    double prev[5];
    size_t i;

    cfg.expressions = exprs;
    cfg.expression_count = 2u;
    cfg.view_sources = views;
    cfg.view_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5u; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        now[i] = cxpr_engine_get_double(session, "mapped_now", &found);
        assert(found);
        prev[i] = cxpr_engine_get_double(session, "mapped_prev", &found);
        assert(found);
    }

    assert(isnan(now[0]));
    assert(now[1] == 100.0);
    assert(now[2] == 100.0);
    assert(now[4] == 300.0);
    assert(isnan(prev[0]));
    assert(isnan(prev[1]));
    assert(prev[2] == 100.0);
    assert(prev[4] == 200.0);

    cxpr_engine_session_free(session);
}

static void test_engine_bare_identifier_alias_preserves_bool(void) {
    typedef struct {
        double close;
    } row;
    static const row rows[2] = {{1.0}, {2.0}};
    const cxpr_expression_def exprs[] = {
        {"trend_up", "close > close[1]"},
        {"long_signal", "trend_up"},
        {"entry", "long_signal"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &rows[0].close, sizeof(row), 2u},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool found = false;
    bool entry = false;

    cfg.expressions = exprs;
    cfg.expression_count = 3u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(cxpr_engine_tick_at(session, 1, NULL, NULL, &err));
    entry = cxpr_engine_get_bool(session, "entry", &found);
    assert(found);
    assert(entry == true);
    cxpr_engine_session_free(session);
}

static void test_engine_inline_lookback_reevaluates_at_offset(void) {
    static const double close[5] = {10.0, 11.0, 9.0, 12.0, 8.0};
    const cxpr_expression_def exprs[] = {
        {"inline_prev", "(close > 10)[1]"},
        {"inline_nested", "(close > close[1])[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 5},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool inline_prev[5];
    bool inline_nested[5];
    size_t i;

    cfg.expressions = exprs;
    cfg.expression_count = 2;
    cfg.column_sources = cols;
    cfg.column_source_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        inline_prev[i] = cxpr_engine_get_bool(session, "inline_prev", &found);
        assert(found);
        inline_nested[i] = cxpr_engine_get_bool(session, "inline_nested", &found);
        assert(found);
    }

    assert(!inline_prev[0]);
    assert(!inline_prev[1]);
    assert(inline_prev[2]);
    assert(inline_prev[4]);
    assert(!inline_nested[0]);
    assert(!inline_nested[1]);
    assert(inline_nested[2]);
    assert(!inline_nested[3]);
    cxpr_engine_session_free(session);
}

static void test_engine_basket_roles_source_args_and_member_lookback(void) {
    static const double prices[5][3] = {
        {1.0, 5.0, 9.0},
        {2.0, 4.0, 12.0},
        {3.0, 6.0, 15.0},
        {4.0, 7.0, 11.0},
        {5.0, 8.0, 10.0},
    };
    static const double members[3] = {1.0, 2.0, 3.0};
    static const double single_member[1] = {2.0};
    test_price_env env = {prices, 0u};
    const cxpr_expression_def exprs[] = {
        {"avg_px", "avg(price($pair))"},
        {"min_px", "min(price($pair))"},
        {"max_px", "max(price($pair))"},
        {"any_hot", "any(price($pair) > 14)"},
        {"all_positive", "all(price($pair) > 0)"},
        {"count_pair", "count($pair)"},
        {"avg_prev", "avg(price($pair)[1])"},
        {"avg_double", "avg(price($pair) + price($pair))"},
        {"single_direct", "$solo"},
    };
    const cxpr_engine_view_source_def views[] = {
        {"price", pair_price_view, &env},
    };
    const cxpr_engine_role_def roles[] = {
        {"pair", members, 3, 5},
        {"solo", single_member, 1},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool found = false;

    cfg.expressions = exprs;
    cfg.expression_count = 9;
    cfg.view_sources = views;
    cfg.view_source_count = 1;
    cfg.roles = roles;
    cfg.role_count = 2;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);

    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_double(session, "avg_px", &found) == 5.0 && found);
    assert(cxpr_engine_get_double(session, "min_px", &found) == 1.0 && found);
    assert(cxpr_engine_get_double(session, "max_px", &found) == 9.0 && found);
    assert(!cxpr_engine_get_bool(session, "any_hot", &found) && found);
    assert(cxpr_engine_get_bool(session, "all_positive", &found) && found);
    assert(cxpr_engine_get_double(session, "count_pair", &found) == 5.0 && found);
    assert(cxpr_engine_get_double(session, "avg_double", &found) == 10.0 && found);
    assert(cxpr_engine_get_double(session, "single_direct", &found) == 2.0 && found);
    assert(isnan(cxpr_engine_get_double(session, "avg_prev", &found)) && found);

    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_double(session, "avg_prev", &found) == 5.0 && found);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_bool(session, "any_hot", &found) && found);

    cxpr_engine_session_free(session);
}

static void test_engine_source_call_memo_reuses_bound_args(void) {
    static const double prices[5][3] = {
        {1.0, 5.0, 9.0},
        {2.0, 4.0, 12.0},
        {3.0, 6.0, 15.0},
        {4.0, 7.0, 11.0},
        {5.0, 8.0, 10.0},
    };
    static const double members[3] = {1.0, 2.0, 3.0};
    test_price_env env = {prices, 0u};
    const cxpr_expression_def exprs[] = {
        {"avg_double", "avg(price($pair) + price($pair))"},
    };
    const cxpr_engine_view_source_def views[] = {
        {"price", pair_price_view, &env},
    };
    const cxpr_engine_role_def roles[] = {
        {"pair", members, 3},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool found = false;

    cfg.expressions = exprs;
    cfg.expression_count = 1;
    cfg.view_sources = views;
    cfg.view_source_count = 1;
    cfg.roles = roles;
    cfg.role_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_double(session, "avg_double", &found) == 10.0 && found);
    assert(env.calls == 3u);
    cxpr_engine_session_free(session);
}

static void test_engine_pull_arg_lookback_uses_per_argument_ring(void) {
    size_t calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"prev", "quote(1)[1]"},
    };
    const cxpr_engine_pull_source_def pulls[] = {
        {"quote", pull_with_args, &calls},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool found = false;

    cfg.expressions = exprs;
    cfg.expression_count = 1;
    cfg.pull_sources = pulls;
    cfg.pull_source_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(isnan(cxpr_engine_get_double(session, "prev", &found)) && found);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_double(session, "prev", &found) == 11.0 && found);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    assert(cxpr_engine_get_double(session, "prev", &found) == 12.0 && found);
    cxpr_engine_session_free(session);
}

/* A host resolver that owns only `hostvar[n]`, counting its invocations. */
static bool host_prior_resolver(const cxpr_expr_ast* target, const cxpr_expr_ast* index,
                                const cxpr_context* ctx, const cxpr_registry* reg,
                                void* userdata, cxpr_value* out, cxpr_error* err) {
    size_t* calls = (size_t*)userdata;
    const char* name;
    double nd;
    (void)ctx;
    (void)reg;
    (void)err;
    if (!target || cxpr_expr_ast_kind_of(target) != CXPR_NODE_IDENTIFIER) return false;
    name = cxpr_expr_ast_identifier_name(target);
    if (!name || strcmp(name, "hostvar") != 0) return false;
    if (calls) (*calls)++;
    nd = (index && cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER) ? cxpr_expr_ast_number_value(index) : 0.0;
    *out = cxpr_num(1000.0 + nd);
    return true;
}

/* The engine serves its own source lookback (`close[n]`) and delegates targets
 * it does not own (`hostvar[n]`) to a resolver the host installed beforehand. */
static void test_engine_lookback_delegates_to_prior_resolver(void) {
    static const double close[5] = {10.0, 11.0, 9.0, 12.0, 8.0};
    size_t host_calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"c1", "close[1]"},
        {"h1", "hostvar[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 5},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double c1[5];
    double h1[5];
    size_t i;

    assert(registry);
    cxpr_register_defaults(registry);
    cxpr_registry_set_lookback_resolver(registry, host_prior_resolver, &host_calls, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 2;
    cfg.column_sources = cols;
    cfg.column_source_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        c1[i] = cxpr_engine_get_double(session, "c1", &found);
        assert(found);
        h1[i] = cxpr_engine_get_double(session, "h1", &found);
        assert(found);
    }

    /* close[1]: engine column offset, warmup NaN at tick 0. */
    assert(isnan(c1[0]));
    assert(c1[1] == 10.0);
    assert(c1[4] == 12.0);
    /* hostvar[1]: served by the delegate every tick (1000 + 1). */
    for (i = 0; i < 5; ++i) assert(h1[i] == 1001.0);
    /* The delegate fires only for hostvar[1], never for the engine-owned close[1]. */
    assert(host_calls == 5u);

    cxpr_engine_session_free(session);

    /* The injected registry is left as found: the host's resolver is restored. */
    {
        cxpr_lookback_resolver_ptr restored = NULL;
        void* restored_ud = NULL;
        cxpr_registry_lookback_resolver(registry, &restored, &restored_ud);
        assert(restored == host_prior_resolver);
        assert(restored_ud == &host_calls);
    }
    cxpr_registry_free(registry); /* injected registry is not engine-owned */
}

static void test_engine_shared_registry_non_engine_lookback_uses_prior_resolver(void) {
    static const double close[3] = {10.0, 11.0, 12.0};
    size_t host_calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"c1", "close[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 3u},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    cxpr_expr_ast* ast;
    double out = 0.0;

    assert(registry);
    assert(ctx);
    assert(parser);
    cxpr_register_defaults(registry);
    cxpr_registry_set_lookback_resolver(registry, host_prior_resolver, &host_calls, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 1u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);

    ast = cxpr_expr_ast_parse(parser, "hostvar[2]", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, registry, &out, &err));
    assert(out == 1002.0);
    assert(host_calls == 1u);

    cxpr_expr_ast_free(ast);
    cxpr_engine_session_free(session);
    cxpr_expr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(registry);
}

static void test_engine_shared_registry_resolver_refcount(void) {
    static const double close[3] = {10.0, 11.0, 12.0};
    size_t host_calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"c1", "close[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 3u},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* first;
    cxpr_engine_session* second;
    bool found = false;

    assert(registry);
    cxpr_register_defaults(registry);
    cxpr_registry_set_lookback_resolver(registry, host_prior_resolver, &host_calls, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 1u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    first = cxpr_engine_session_create(&cfg, &err);
    assert(first);
    second = cxpr_engine_session_create(&cfg, &err);
    assert(second);

    assert(cxpr_engine_tick(first, NULL, NULL, &err));
    assert(isnan(cxpr_engine_get_double(first, "c1", &found)) && found);

    cxpr_engine_session_free(first);

    assert(cxpr_engine_tick(second, NULL, NULL, &err));
    assert(isnan(cxpr_engine_get_double(second, "c1", &found)) && found);
    assert(cxpr_engine_tick(second, NULL, NULL, &err));
    assert(cxpr_engine_get_double(second, "c1", &found) == 10.0 && found);
    assert(host_calls == 0u);

    cxpr_engine_session_free(second);
    {
        cxpr_lookback_resolver_ptr restored = NULL;
        void* restored_ud = NULL;
        cxpr_registry_lookback_resolver(registry, &restored, &restored_ud);
        assert(restored == host_prior_resolver);
        assert(restored_ud == &host_calls);
    }
    cxpr_registry_free(registry);
}

static void test_engine_tracked_struct_field_lookback_uses_result_ring(void) {
    static const double close[5] = {10.0, 12.0, 15.0, 11.0, 20.0};
    const cxpr_expression_def exprs[] = {
        {"band", "band(close)"},
        {"prev_lower", "band.lower[1]"},
        {"prev_upper2", "band.upper[2]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 5},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double prev_lower[5];
    double prev_upper2[5];
    size_t i;

    assert(registry);
    cxpr_register_defaults(registry);
    cxpr_registry_add_value(registry, "band", band_value_fn, 1u, 1u, NULL, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 3u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5u; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        prev_lower[i] = cxpr_engine_get_double(session, "prev_lower", &found);
        assert(found);
        prev_upper2[i] = cxpr_engine_get_double(session, "prev_upper2", &found);
        assert(found);
    }

    assert(isnan(prev_lower[0]));
    assert(prev_lower[1] == 9.0);
    assert(prev_lower[4] == 10.0);
    assert(isnan(prev_upper2[0]));
    assert(isnan(prev_upper2[1]));
    assert(prev_upper2[3] == 13.0);

    cxpr_engine_session_free(session);
    cxpr_registry_free(registry);
}

static void test_engine_tracked_nested_struct_path_lookback_uses_result_ring(void) {
    static const double close[5] = {10.0, 12.0, 15.0, 11.0, 20.0};
    const cxpr_expression_def exprs[] = {
        {"box", "box(close)"},
        {"prev_value", "box.inner.value[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 5},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double prev_value[5];
    size_t i;

    assert(registry);
    cxpr_register_defaults(registry);
    cxpr_registry_add_value(registry, "box", nested_box_value_fn, 1u, 1u, NULL, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 2u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 5u; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        prev_value[i] = cxpr_engine_get_double(session, "prev_value", &found);
        assert(found);
    }

    assert(isnan(prev_value[0]));
    assert(prev_value[1] == 10.5);
    assert(prev_value[4] == 11.5);

    cxpr_engine_session_free(session);
    cxpr_registry_free(registry);
}

static void test_engine_inline_lookback_policy_preempts_prior_resolver(void) {
    static const double close[4] = {10.0, 11.0, 9.0, 12.0};
    size_t delegate_calls = 0u;
    size_t policy_calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"prev_shifted", "shifted(close)[1]"},
    };
    const cxpr_engine_column_source_def cols[] = {
        {"close", &close[0], sizeof(double), 4},
    };
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    double values[4];
    size_t i;

    assert(registry);
    cxpr_register_defaults(registry);
    cxpr_registry_add_ast(
        registry, "shifted", shifted_ast_fn, 1u, 1u, CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_set_lookback_resolver(
        registry, host_counting_resolver, &delegate_calls, NULL);

    cfg.registry = registry;
    cfg.expressions = exprs;
    cfg.expression_count = 1u;
    cfg.column_sources = cols;
    cfg.column_source_count = 1u;
    cfg.inline_lookback = inline_shifted_policy;
    cfg.inline_lookback_userdata = &policy_calls;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    for (i = 0; i < 4u; ++i) {
        bool found = false;
        assert(cxpr_engine_tick(session, NULL, NULL, &err));
        values[i] = cxpr_engine_get_double(session, "prev_shifted", &found);
        assert(found);
    }

    assert(isnan(values[0]));
    assert(values[1] == 110.0);
    assert(values[2] == 111.0);
    assert(values[3] == 109.0);
    assert(delegate_calls == 0u);
    assert(policy_calls == 4u);

    cxpr_engine_session_free(session);
    cxpr_registry_free(registry);
}

static void test_engine_qualified_param_names(void) {
    const cxpr_expression_def exprs[] = {
        {"sum", "$base.period + $period"},
    };
    const cxpr_context_entry params[] = {
        {"base.period", 8.0},
        {"period", 5.0},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;
    bool found = false;
    double value;

    cfg.expressions = exprs;
    cfg.expression_count = 1u;
    cfg.params = params;
    cfg.param_count = 2u;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(cxpr_engine_tick(session, NULL, NULL, &err));
    value = cxpr_engine_get_double(session, "sum", &found);
    assert(found);
    assert(value == 13.0);
    cxpr_engine_session_free(session);
}

int main(void) {
    test_engine_view_source_lookback();
    test_engine_view_source_map_index_before_read();
    test_engine_bare_identifier_alias_preserves_bool();
    test_engine_lookback_delegates_to_prior_resolver();
    test_engine_shared_registry_non_engine_lookback_uses_prior_resolver();
    test_engine_shared_registry_resolver_refcount();
    test_engine_tracked_struct_field_lookback_uses_result_ring();
    test_engine_tracked_nested_struct_path_lookback_uses_result_ring();
    test_engine_inline_lookback_policy_preempts_prior_resolver();
    test_engine_inline_lookback_reevaluates_at_offset();
    test_engine_basket_roles_source_args_and_member_lookback();
    test_engine_source_call_memo_reuses_bound_args();
    test_engine_pull_arg_lookback_uses_per_argument_ring();
    test_engine_qualified_param_names();
    printf("  \xE2\x9C\x93 engine_layer\n");
    return 0;
}
