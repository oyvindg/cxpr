/* Increment-1 verification for the cxpr engine layer.
 * Covers: all four edge kinds, params (default + override), basket roles
 * (avg/count), session reset. Run under ASAN/UBSAN. Exit 0 = all pass. */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); ++failures; } \
    else { printf("ok:   %s\n", (msg)); } \
} while (0)

/* Count events over a fixed bar set for one engine config. */
static int run_count_events(const cxpr_engine_config* cfg, const double* px,
                            int bars, const char* label) {
    cxpr_error err = {0};
    cxpr_engine_session* s = cxpr_engine_session_create(cfg, &err);
    int total = 0;
    if (!s) { printf("FAIL: %s session_create: %s\n", label, err.message ? err.message : "?"); ++failures; return -1; }
    for (int i = 0; i < bars; ++i) {
        const cxpr_engine_event* ev; size_t n;
        (void)px;
        if (!cxpr_engine_tick(s, &ev, &n, &err)) { printf("FAIL: %s tick %d: %s\n", label, i, err.message ? err.message : "?"); ++failures; cxpr_engine_session_free(s); return -1; }
        total += (int)n;
    }
    cxpr_engine_session_free(s);
    return total;
}

int main(void) {
    static const double close[5] = { 10.0, 11.0, 9.0, 12.0, 8.0 };

    /* ---- edges ---- buy = close>10 -> [F,T,F,T,F]; px = close ---- */
    {
        const cxpr_expression_def ex[] = { { "buy", "close > 10" }, { "px", "close" } };
        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        const cxpr_engine_watch_def w[] = {
            { "buy", CXPR_EDGE_RISING }, { "buy", CXPR_EDGE_FALLING },
            { "buy", CXPR_EDGE_LEVEL },  { "px", CXPR_EDGE_CHANGED },
        };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 2;
        cfg.column_sources = cols; cfg.column_source_count = 1;
        cfg.watches = w; cfg.watch_count = 4;
        int n = run_count_events(&cfg, close, 5, "edges");
        /* RISING@1,3 (2) FALLING@2,4 (2) LEVEL@1,3 (2) CHANGED@1,2,3,4 (4) = 10 */
        CHECK(n == 10, "all-edge event total == 10");
    }

    /* ---- params: default vs override ---- */
    {
        const cxpr_expression_def ex[] = { { "sig", "close > $thresh" } };
        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        const cxpr_context_entry params[] = { { "thresh", 10.0 }, { NULL, 0 } };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 1;
        cfg.column_sources = cols; cfg.column_source_count = 1;
        cfg.params = params; cfg.param_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "param session created");
        if (s) {
            bool f;
            cxpr_engine_tick(s, NULL, NULL, &err);                 /* close=10, thresh=10 */
            CHECK(cxpr_engine_get_bool(s, "sig", &f) == false, "sig false at close=10 thresh=10");
            cxpr_engine_tick(s, NULL, NULL, &err);                 /* close=11, thresh=10 */
            CHECK(cxpr_engine_get_bool(s, "sig", &f) == true,  "sig true at close=11 thresh=10");
            cxpr_engine_set_param(s, "thresh", 12.0);              /* raise threshold */
            cxpr_engine_tick(s, NULL, NULL, &err);                 /* close=9, thresh=12 */
            CHECK(cxpr_engine_get_bool(s, "sig", &f) == false, "sig false at close=9 thresh=12");
            cxpr_engine_session_free(s);
        }
    }

    /* ---- basket: avg + count over a role of member ids ---- */
    {
        const cxpr_expression_def ex[] = { { "m", "avg($grp)" }, { "c", "count($grp)" } };
        const double members[] = { 2.0, 4.0, 6.0 };
        const cxpr_engine_role_def roles[] = { { "grp", members, 3 } };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 2;
        cfg.roles = roles; cfg.role_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "basket session created");
        if (s) {
            bool f;
            cxpr_engine_tick(s, NULL, NULL, &err);
            double m = cxpr_engine_get_double(s, "m", &f);
            double c = cxpr_engine_get_double(s, "c", &f);
            CHECK(fabs(m - 4.0) < 1e-9, "avg($grp) == 4");
            CHECK(fabs(c - 3.0) < 1e-9, "count($grp) == 3");
            /* dynamic membership */
            const double m2[] = { 10.0, 20.0 };
            cxpr_engine_set_role(s, "grp", m2, 2);
            cxpr_engine_tick(s, NULL, NULL, &err);
            m = cxpr_engine_get_double(s, "m", &f);
            c = cxpr_engine_get_double(s, "c", &f);
            CHECK(fabs(m - 15.0) < 1e-9, "avg($grp) == 15 after set_role");
            CHECK(fabs(c - 2.0) < 1e-9, "count($grp) == 2 after set_role");
            cxpr_engine_session_free(s);
        }
    }

    /* ---- reset: RISING fires again after reset ---- */
    {
        const cxpr_expression_def ex[] = { { "buy", "close > 10" } };
        const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
        const cxpr_engine_watch_def w[] = { { "buy", CXPR_EDGE_RISING } };
        cxpr_engine_config cfg = {0};
        cfg.expressions = ex; cfg.expression_count = 1;
        cfg.column_sources = cols; cfg.column_source_count = 1;
        cfg.watches = w; cfg.watch_count = 1;

        cxpr_error err = {0};
        cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
        CHECK(s != NULL, "reset session created");
        if (s) {
            const cxpr_engine_event* ev; size_t n; int before = 0, after = 0;
            for (int i = 0; i < 5; ++i) { cxpr_engine_tick(s, &ev, &n, &err); before += (int)n; }
            cxpr_engine_session_reset(s);
            CHECK(cxpr_engine_tick_index(s) == -1, "cursor reset to -1");
            for (int i = 0; i < 5; ++i) { cxpr_engine_tick(s, &ev, &n, &err); after += (int)n; }
            CHECK(before == 2 && after == 2, "RISING count identical before/after reset (2)");
            cxpr_engine_session_free(s);
        }
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
