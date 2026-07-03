/* cxpr_engine with context slots for host-owned hot-loop inputs.
 *
 * Build from libs/cxpr:
 *   cc examples/engine_slots.c -Iinclude -Lbuild -lcxpr -lm -o /tmp/cxpr_engine_slots
 */
#include <cxpr/cxpr.h>
#include <cxpr/engine.h>

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    double open;
    double high;
    double low;
    double close;
    double volume;
} bar;

static double number_or_nan(cxpr_value value) {
    return value.type == CXPR_VALUE_NUMBER ? value.d : 0.0 / 0.0;
}

static int bool_or_zero(cxpr_value value) {
    if (value.type == CXPR_VALUE_BOOL) return value.b ? 1 : 0;
    if (value.type == CXPR_VALUE_NUMBER) return value.d != 0.0 ? 1 : 0;
    return 0;
}

int main(void) {
    static const bar bars[] = {
        {100.0, 101.0,  99.5,  99.8, 1200.0},
        {100.0, 103.0,  99.0, 102.5, 1800.0},
        {102.5, 104.0, 101.0, 103.0, 2000.0},
        {103.0, 103.5, 101.5, 102.0, 1600.0},
        {102.0, 106.5, 101.5, 105.5, 2200.0},
    };

    const cxpr_expression_def exprs[] = {
        {"range", "high - low"},
        {"turnover", "close * volume"},
        {"liquid_breakout", "close > open and range >= $min_range and turnover >= $min_turnover"},
    };
    const cxpr_context_entry params[] = {
        {"min_range", 3.0},
        {"min_turnover", 180000.0},
    };
    const cxpr_engine_watch_def watches[] = {
        {"liquid_breakout", CXPR_EDGE_RISING},
    };
    const cxpr_engine_config cfg = {
        .expressions = exprs,
        .expression_count = CXPR_ARRAY_COUNT(exprs),
        .params = params,
        .param_count = CXPR_ARRAY_COUNT(params),
        .watches = watches,
        .watch_count = CXPR_ARRAY_COUNT(watches),
    };

    cxpr_error err = {0};
    cxpr_engine_session* session = cxpr_engine_session_create(&cfg, &err);
    if (!session) {
        fprintf(stderr, "session_create failed: %s\n", err.message ? err.message : "?");
        return 1;
    }

    cxpr_context* ctx = cxpr_engine_session_context(session);
    const char* slot_names[] = {"open", "high", "low", "close", "volume"};
    cxpr_context_slot slots[5];

    for (size_t i = 0; i < CXPR_ARRAY_COUNT(slot_names); ++i) {
        cxpr_context_set(ctx, slot_names[i], 0.0);
    }
    if (!cxpr_context_slots_bind(ctx, slot_names, slots, CXPR_ARRAY_COUNT(slots))) {
        fprintf(stderr, "failed to bind input slots\n");
        cxpr_engine_session_free(session);
        return 1;
    }

    int total_events = 0;
    for (size_t i = 0; i < CXPR_ARRAY_COUNT(bars); ++i) {
        const double values[] = {
            bars[i].open,
            bars[i].high,
            bars[i].low,
            bars[i].close,
            bars[i].volume,
        };
        cxpr_context_slots_set(slots, values, CXPR_ARRAY_COUNT(slots));

        const cxpr_engine_event* events = NULL;
        size_t event_count = 0;
        if (!cxpr_engine_tick(session, &events, &event_count, &err)) {
            fprintf(stderr, "tick %zu failed: %s\n", i, err.message ? err.message : "?");
            cxpr_engine_session_free(session);
            return 1;
        }

        bool found = false;
        cxpr_value range_value = cxpr_engine_get(session, "range", &found);
        cxpr_value signal_value = cxpr_engine_get(session, "liquid_breakout", &found);
        printf("tick %lld close=%.1f range=%.1f signal=%d events=%zu\n",
               (long long)cxpr_engine_tick_index(session),
               bars[i].close,
               number_or_nan(range_value),
               bool_or_zero(signal_value),
               event_count);

        for (size_t e = 0; e < event_count; ++e) {
            printf("    EVENT %s edge=%d\n", events[e].expr_name, (int)events[e].edge);
            ++total_events;
        }
    }

    cxpr_engine_session_free(session);
    printf("total RISING events = %d (expected 2)\n", total_events);
    return total_events == 2 ? 0 : 2;
}
