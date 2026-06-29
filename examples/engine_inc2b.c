/* Increment-2b verification: named-expression result rings (expr[n], D16) and
 * registry-defined expressions as watch targets (D15). Run under ASAN/UBSAN. */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); ++failures; } \
    else { printf("ok:   %s\n", (msg)); } \
} while (0)

static bool pair_price_view(int64_t index,
                            const char* name,
                            const double* args,
                            size_t argc,
                            double* out,
                            void* ud) {
    const double (*prices)[3] = (const double (*)[3])ud;
    int pair;
    (void)name;
    if (!prices || !out || index < 0 || index >= 5 || argc != 1) return false;
    pair = (int)args[0];
    if (pair < 1 || pair > 3) return false;
    *out = prices[index][pair - 1];
    return true;
}

int main(void) {
    static const double close[5] = { 10.0, 11.0, 9.0, 12.0, 8.0 };

    /* ---- named-expression result rings: prev = px[1], rose = px > px[1] ---- */
    {
        const cxpr_expression_def ex[] = {
            { "px", "close" }, { "prev", "px[1]" }, { "rose", "px > px[1]" },
            { "inline_prev", "(close > 10)[1]" },
            { "inline_nested", "(close > close[1])[1]" },
        };
        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 5;
        cfg.column_sources = cols; cfg.column_source_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "expr-ring session created");
        if (s) {
            double prev[5]; bool rose[5]; bool inline_prev[5]; bool inline_nested[5];
            for (int i = 0; i < 5; ++i) {
                bool f;
                cxpr_engine_tick(s, NULL, NULL, &err);
                prev[i] = cxpr_engine_get_double(s, "prev", &f);
                rose[i] = cxpr_engine_get_bool(s, "rose", &f);
                inline_prev[i] = cxpr_engine_get_bool(s, "inline_prev", &f);
                inline_nested[i] = cxpr_engine_get_bool(s, "inline_nested", &f);
                printf("t%d close=%.0f prev=%.2f rose=%d\n", i, close[i], prev[i], (int)rose[i]);
            }
            CHECK(isnan(prev[0]), "prev@t0 NaN (expr-ring warmup)");
            CHECK(prev[1] == 10.0, "prev@t1 == px@t0 == 10");
            CHECK(prev[2] == 11.0, "prev@t2 == px@t1 == 11");
            CHECK(prev[4] == 12.0, "prev@t4 == px@t3 == 12");
            CHECK(rose[1] && rose[3] && !rose[2] && !rose[0], "rose true@t1,t3 false@t0,t2");
            CHECK(!inline_prev[0] && !inline_prev[1] && inline_prev[2] && inline_prev[4],
                  "(close > 10)[1] follows prior bar");
            CHECK(!inline_nested[0] && !inline_nested[1] && inline_nested[2] && !inline_nested[3],
                  "(close > close[1])[1] re-evaluates nested lookback at offset");
            cxpr_engine_session_free(s);
        }
    }

    /* ---- D25: basket roles with all folds, source args, per-member lookback ---- */
    {
        static const double prices[5][3] = {
            { 1.0,  5.0,  9.0 },
            { 2.0,  4.0, 12.0 },
            { 3.0,  6.0, 15.0 },
            { 4.0,  7.0, 11.0 },
            { 5.0,  8.0, 10.0 },
        };
        static const double members[3] = { 1.0, 2.0, 3.0 };
        static const double single_member[1] = { 2.0 };
        const cxpr_expression_def ex[] = {
            { "avg_px", "avg(price($pair))" },
            { "min_px", "min(price($pair))" },
            { "max_px", "max(price($pair))" },
            { "any_hot", "any(price($pair) > 14)" },
            { "all_positive", "all(price($pair) > 0)" },
            { "count_pair", "count($pair)" },
            { "avg_prev", "avg(price($pair)[1])" },
            { "single_direct", "$solo" },
        };
        const cxpr_engine_view_source_def views[] = {
            { "price", pair_price_view, (void*)prices },
        };
        const cxpr_engine_role_def roles[] = {
            { "pair", members, 3 },
            { "solo", single_member, 1 },
        };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 8;
        cfg.view_sources = views; cfg.view_source_count = 1;
        cfg.roles = roles; cfg.role_count = 2;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "basket source-arg session created");
        if (s) {
            bool f;
            cxpr_engine_tick(s, NULL, NULL, &err);
            CHECK(cxpr_engine_get_double(s, "avg_px", &f) == 5.0, "avg(price($pair)) at t0");
            CHECK(cxpr_engine_get_double(s, "min_px", &f) == 1.0, "min(price($pair)) at t0");
            CHECK(cxpr_engine_get_double(s, "max_px", &f) == 9.0, "max(price($pair)) at t0");
            CHECK(!cxpr_engine_get_bool(s, "any_hot", &f), "any false before hot member");
            CHECK(cxpr_engine_get_bool(s, "all_positive", &f), "all positive true");
            CHECK(cxpr_engine_get_double(s, "count_pair", &f) == 3.0, "count($pair) == 3");
            CHECK(cxpr_engine_get_double(s, "single_direct", &f) == 2.0, "single-member $role binding");
            CHECK(isnan(cxpr_engine_get_double(s, "avg_prev", &f)), "avg(price($pair)[1]) warmup NaN");

            cxpr_engine_tick(s, NULL, NULL, &err);
            CHECK(cxpr_engine_get_double(s, "avg_prev", &f) == 5.0,
                  "avg(price($pair)[1]) uses per-member view lookback");
            cxpr_engine_tick(s, NULL, NULL, &err);
            CHECK(cxpr_engine_get_bool(s, "any_hot", &f), "any true after hot member");
            cxpr_engine_session_free(s);
        }
    }

    /* ---- D15: watch a registry-defined expression (references a source) ---- */
    {
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_register_defaults(reg);
        cxpr_error derr = cxpr_registry_define_fn(reg, "hot() => close > 10");
        CHECK(derr.code == CXPR_OK, "registry define_fn hot()");

        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        const cxpr_engine_watch_def w[] = { { "hot", CXPR_EDGE_RISING } };
        cxpr_engine_config cfg = {0};
        cfg.registry = reg;                 /* injected; "hot" lives here, not in config */
        cfg.column_sources = cols; cfg.column_source_count = 1;
        cfg.watches = w; cfg.watch_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "registry-watch session created");
        if (s) {
            const cxpr_engine_event* ev; size_t n; int total = 0;
            for (int i = 0; i < 5; ++i) {
                cxpr_engine_tick(s, &ev, &n, &err);
                total += (int)n;
            }
            CHECK(total == 2, "registry-defined 'hot' RISING fires twice");
            cxpr_engine_session_free(s);
        }
        cxpr_registry_free(reg);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
