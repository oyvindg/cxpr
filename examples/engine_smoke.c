/* Smoke test for the cxpr engine layer (cxpr/engine.h).
 * Build: cc engine_smoke.c -I../include -L<build> -lcxpr -lm -o smoke */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    /* 5 "bars": one close column. */
    static const double close[5] = { 10.0, 11.0, 9.0, 12.0, 8.0 };

    const cxpr_expression_def exprs[] = {
        { "buy", "close > 10" },
    };
    const cxpr_engine_column_source_def cols[] = {
        { "close", &close[0], sizeof(double), 5 },
    };
    const cxpr_engine_watch_def watches[] = {
        { "buy", CXPR_EDGE_RISING },
    };
    const cxpr_engine_config cfg = {
        .expressions = exprs,    .expression_count = 1,
        .column_sources = cols,  .column_source_count = 1,
        .watches = watches,      .watch_count = 1,
        /* registry NULL -> engine builds a defaults registry it owns (D19). */
    };

    cxpr_error err = {0};
    cxpr_engine_session* s = cxpr_engine_session_create(&cfg, &err);
    if (!s) {
        fprintf(stderr, "session_create failed: %s\n", err.message ? err.message : "?");
        return 1;
    }

    int total_events = 0;
    for (int i = 0; i < 5; ++i) {
        const cxpr_engine_event* ev;
        size_t n;
        if (!cxpr_engine_tick(s, &ev, &n, &err)) {
            fprintf(stderr, "tick %d failed: %s\n", i, err.message ? err.message : "?");
            cxpr_engine_session_free(s);
            return 1;
        }
        bool found;
        double buy = cxpr_engine_get_double(s, "buy", &found);
        printf("tick %lld close=%.1f buy=%.0f events=%zu\n",
               (long long)cxpr_engine_tick_index(s), close[i], buy, n);
        for (size_t e = 0; e < n; ++e) {
            printf("    EVENT %s edge=%d value=%.0f\n",
                   ev[e].expr_name, (int)ev[e].edge, ev[e].value.d);
            ++total_events;
        }
    }

    cxpr_engine_session_free(s);
    printf("total RISING events = %d (expected 2)\n", total_events);
    return total_events == 2 ? 0 : 2;
}
