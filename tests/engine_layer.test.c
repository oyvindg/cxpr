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

static double pull_with_args(const char* name, const double* args, size_t argc, void* userdata) {
    size_t* calls = (size_t*)userdata;
    (void)name;
    if (calls) (*calls)++;
    return argc > 0u ? args[0] : 0.0;
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
        {"pair", members, 3},
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
    assert(cxpr_engine_get_double(session, "count_pair", &found) == 3.0 && found);
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

static void test_engine_rejects_pull_arg_lookback_without_arg_rings(void) {
    size_t calls = 0u;
    const cxpr_expression_def exprs[] = {
        {"bad", "quote(1)[1]"},
    };
    const cxpr_engine_pull_source_def pulls[] = {
        {"quote", pull_with_args, &calls},
    };
    cxpr_engine_config cfg = {0};
    cxpr_error err = {0};
    cxpr_engine_session* session;

    cfg.expressions = exprs;
    cfg.expression_count = 1;
    cfg.pull_sources = pulls;
    cfg.pull_source_count = 1;

    session = cxpr_engine_session_create(&cfg, &err);
    assert(session);
    assert(!cxpr_engine_tick(session, NULL, NULL, &err));
    assert(err.code != CXPR_OK);
    cxpr_engine_session_free(session);
}

/* A host resolver that owns only `hostvar[n]`, counting its invocations. */
static bool host_prior_resolver(const cxpr_ast* target, const cxpr_ast* index,
                                const cxpr_context* ctx, const cxpr_registry* reg,
                                void* userdata, cxpr_value* out, cxpr_error* err) {
    size_t* calls = (size_t*)userdata;
    const char* name;
    double nd;
    (void)ctx;
    (void)reg;
    (void)err;
    if (!target || cxpr_ast_type(target) != CXPR_NODE_IDENTIFIER) return false;
    name = cxpr_ast_identifier_name(target);
    if (!name || strcmp(name, "hostvar") != 0) return false;
    if (calls) (*calls)++;
    nd = (index && cxpr_ast_type(index) == CXPR_NODE_NUMBER) ? cxpr_ast_number_value(index) : 0.0;
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

int main(void) {
    test_engine_view_source_lookback();
    test_engine_lookback_delegates_to_prior_resolver();
    test_engine_inline_lookback_reevaluates_at_offset();
    test_engine_basket_roles_source_args_and_member_lookback();
    test_engine_source_call_memo_reuses_bound_args();
    test_engine_rejects_pull_arg_lookback_without_arg_rings();
    printf("  \xE2\x9C\x93 engine_layer\n");
    return 0;
}
