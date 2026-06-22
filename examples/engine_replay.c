/* Example + D3 watchdog for cxpr_engine_replay (cxpr/engine.h).
 *
 * Deliberately non-trading: a temperature sensor log is replayed through the
 * engine in one call, with no session visible to the host and no per-tick
 * intervention. If this stops reading naturally for a sensor/IoT consumer, the
 * replay API has leaked trading domain (D3).
 *
 * Build: cc engine_replay.c -I../include -L<build> -lcxpr -lm -o replay */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    /* A recorded sensor log: temperature readings, replayed in order. */
    static const double temperature[8] = {
        20.0, 25.0, 31.0, 35.0, 28.0, 32.0, 33.0, 18.0,
    };
    enum { N = 8 };

    const cxpr_expression_def exprs[] = {
        { "overheat", "temperature > 30" },
    };
    const cxpr_engine_column_source_def cols[] = {
        { "temperature", &temperature[0], sizeof(double), N },
    };
    /* Watch both edges: when it crosses into and out of overheat. */
    const cxpr_engine_watch_def watches[] = {
        { "overheat", CXPR_EDGE_RISING },
        { "overheat", CXPR_EDGE_FALLING },
    };
    const cxpr_engine_config cfg = {
        .expressions = exprs,    .expression_count = 1,
        .column_sources = cols,  .column_source_count = 1,
        .watches = watches,      .watch_count = 2,
        /* registry NULL -> engine builds and owns a defaults registry (D19). */
    };

    /* One call: build, replay all N readings, collect every event, tear down. */
    cxpr_engine_event* events = NULL;
    size_t count = 0;
    cxpr_error err = {0};
    if (!cxpr_engine_replay(&cfg, N, &events, &count, &err)) {
        fprintf(stderr, "replay failed: %s\n", err.message ? err.message : "?");
        return 1;
    }

    int rising = 0, falling = 0;
    for (size_t i = 0; i < count; ++i) {
        printf("EVENT %s edge=%s\n",
               events[i].expr_name,
               events[i].edge == CXPR_EDGE_RISING ? "RISING" : "FALLING");
        if (events[i].edge == CXPR_EDGE_RISING) ++rising;
        else ++falling;
    }
    cxpr_engine_events_free(events, count);

    /* overheat (temp>30): F F T T F T T F
     *   RISING  at idx 2 and 5  -> 2
     *   FALLING at idx 4 and 7  -> 2 */
    printf("rising=%d (expected 2) falling=%d (expected 2)\n", rising, falling);
    return (rising == 2 && falling == 2) ? 0 : 2;
}
