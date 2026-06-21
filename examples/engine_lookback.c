/* Increment-2 verification: source lookback (column offset + pull ring) and
 * warmup NaN. Run under ASAN/UBSAN. Exit 0 = all pass. */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <math.h>
#include <stdio.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); ++failures; } \
    else { printf("ok:   %s\n", (msg)); } \
} while (0)

/* Pull source: returns an incrementing counter, once per tick. */
static double seq_cb(const char* name, const double* args, size_t argc, void* ud) {
    (void)name; (void)args; (void)argc;
    int* c = (int*)ud;
    return (double)((*c)++);
}

int main(void) {
    static const double close[5] = { 10.0, 11.0, 9.0, 12.0, 8.0 };
    int seq_counter = 0;

    const cxpr_expression_def ex[] = {
        { "c0", "close" }, { "c1", "close[1]" }, { "c2", "close[2]" },
        { "s0", "seq" },   { "s1", "seq[1]" },
    };
    const cxpr_engine_column_source_def cols[] = { { "close", &close[0], sizeof(double), 5 } };
    const cxpr_engine_pull_source_def pulls[] = { { "seq", seq_cb, &seq_counter } };

    cxpr_engine_config cfg = {0};
    cfg.expressions = ex; cfg.expression_count = 5;
    cfg.column_sources = cols; cfg.column_source_count = 1;
    cfg.pull_sources = pulls; cfg.pull_source_count = 1;

    cxpr_error err = {0};
    cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
    CHECK(s != NULL, "lookback session created");
    if (!s) { printf("err: %s\n", err.message ? err.message : "?"); return 1; }

    double c0[5], c1[5], c2[5], s0[5], s1[5];
    for (int i = 0; i < 5; ++i) {
        bool f;
        if (!cxpr_engine_tick(s, NULL, NULL, &err)) {
            printf("FAIL: tick %d: %s\n", i, err.message ? err.message : "?"); ++failures; break;
        }
        c0[i] = cxpr_engine_get_double(s, "c0", &f);
        c1[i] = cxpr_engine_get_double(s, "c1", &f);
        c2[i] = cxpr_engine_get_double(s, "c2", &f);
        s0[i] = cxpr_engine_get_double(s, "s0", &f);
        s1[i] = cxpr_engine_get_double(s, "s1", &f);
        printf("t%d close=%.0f  c1=%.2f c2=%.2f  seq=%.0f s1=%.2f\n",
               i, close[i], c1[i], c2[i], s0[i], s1[i]);
    }

    /* column offset lookback */
    CHECK(c0[2] == 9.0,  "c0@t2 == close[t2] == 9");
    CHECK(c1[2] == 11.0, "c1@t2 == close[t1] == 11");
    CHECK(c2[2] == 10.0, "c2@t2 == close[t0] == 10");
    CHECK(c1[4] == 12.0, "c1@t4 == close[t3] == 12");
    CHECK(c2[4] == 9.0,  "c2@t4 == close[t2] == 9");
    /* warmup -> NaN */
    CHECK(isnan(c1[0]), "c1@t0 NaN (warmup)");
    CHECK(isnan(c2[0]) && isnan(c2[1]), "c2@t0,t1 NaN (warmup)");
    /* pull ring lookback */
    CHECK(s0[2] == 2.0, "seq@t2 == 2");
    CHECK(s1[2] == 1.0, "seq[1]@t2 == 1 (ring)");
    CHECK(isnan(s1[0]), "seq[1]@t0 NaN (ring warmup)");
    CHECK(s1[4] == 3.0, "seq[1]@t4 == 3 (ring)");

    cxpr_engine_session_free(s);
    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
