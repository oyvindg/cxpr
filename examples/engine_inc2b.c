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

int main(void) {
    static const double close[5] = { 10.0, 11.0, 9.0, 12.0, 8.0 };

    /* ---- named-expression result rings: prev = px[1], rose = px > px[1] ---- */
    {
        const cxpr_expression_def ex[] = {
            { "px", "close" }, { "prev", "px[1]" }, { "rose", "px > px[1]" },
        };
        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 3;
        cfg.column_sources = cols; cfg.column_source_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "expr-ring session created");
        if (s) {
            double prev[5]; bool rose[5];
            for (int i = 0; i < 5; ++i) {
                bool f;
                cxpr_engine_tick(s, NULL, NULL, &err);
                prev[i] = cxpr_engine_get_double(s, "prev", &f);
                rose[i] = cxpr_engine_get_bool(s, "rose", &f);
                printf("t%d close=%.0f prev=%.2f rose=%d\n", i, close[i], prev[i], (int)rose[i]);
            }
            CHECK(isnan(prev[0]), "prev@t0 NaN (expr-ring warmup)");
            CHECK(prev[1] == 10.0, "prev@t1 == px@t0 == 10");
            CHECK(prev[2] == 11.0, "prev@t2 == px@t1 == 11");
            CHECK(prev[4] == 12.0, "prev@t4 == px@t3 == 12");
            CHECK(rose[1] && rose[3] && !rose[2] && !rose[0], "rose true@t1,t3 false@t0,t2");
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
