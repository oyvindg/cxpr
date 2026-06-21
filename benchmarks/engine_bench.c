/* Benchmark: hand-rolled low-level evaluator loop vs the engine layer.
 *
 * Both paths evaluate the same expression set over the same OHLCV bars and
 * detect the same RISING signals. The low-level path mirrors how a host (dyn)
 * drives the evaluator by hand: set source vars -> eval -> read -> compare
 * against the previous bar. The engine path is one cxpr_engine_tick per bar.
 *
 * Prints ns/bar for each path and the ratio, and asserts both detect the same
 * number of signals (a cheap engine-vs-low-level parity cross-check). */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NBARS 4096u
#define REPS  3000u

static volatile double g_sink = 0.0;

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Same expression set for both paths. */
static const cxpr_expression_def EXPRS[] = {
    { "buy",   "close > open && (high - low) > 0.5" },
    { "exit",  "close < open" },
    { "score", "(close - open) / (high - low + 0.0001)" },
};
enum { NEXPR = (int)(sizeof(EXPRS) / sizeof(EXPRS[0])) };

int main(void) {
    static double open_[NBARS], high_[NBARS], low_[NBARS], close_[NBARS];
    unsigned i, r;
    uint64_t s = 0x9e3779b97f4a7c15ULL; /* deterministic LCG, no Math.random */

    for (i = 0; i < NBARS; ++i) {
        double o, c, h, l;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        o = 100.0 + (double)((s >> 33) % 1000) / 100.0;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        c = 100.0 + (double)((s >> 33) % 1000) / 100.0;
        h = (o > c ? o : c) + 0.75;
        l = (o < c ? o : c) - 0.75;
        open_[i] = o; close_[i] = c; high_[i] = h; low_[i] = l;
    }

    cxpr_error err = {0};

    /* ---------------- low-level hand-rolled loop ---------------- */
    long long low_ns = 0;
    long long low_signals = 0;
    {
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_register_defaults(reg);
        cxpr_evaluator* ev = cxpr_evaluator_new(reg);
        if (!cxpr_expressions_add(ev, EXPRS, NEXPR, &err) || !cxpr_evaluator_compile(ev, &err)) {
            fprintf(stderr, "low-level compile failed: %s\n", err.message ? err.message : "?");
            return 1;
        }
        cxpr_context* ctx = cxpr_context_new();

        long long start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool prev_buy = false, prev_exit = false;
            for (i = 0; i < NBARS; ++i) {
                bool f;
                cxpr_context_set(ctx, "open", open_[i]);
                cxpr_context_set(ctx, "high", high_[i]);
                cxpr_context_set(ctx, "low", low_[i]);
                cxpr_context_set(ctx, "close", close_[i]);
                cxpr_evaluator_eval(ev, ctx, &err);
                bool buy = cxpr_expression_get_bool(ev, "buy", &f);
                bool ex = cxpr_expression_get_bool(ev, "exit", &f);
                g_sink += cxpr_expression_get_double(ev, "score", &f);
                if (buy && !prev_buy) ++low_signals;
                if (ex && !prev_exit) ++low_signals;
                prev_buy = buy; prev_exit = ex;
            }
        }
        low_ns = now_ns() - start;
        cxpr_context_free(ctx);
        cxpr_evaluator_free(ev);
        cxpr_registry_free(reg);
    }

    /* ---------------- engine layer ---------------- */
    long long eng_ns = 0;
    long long eng_signals = 0;
    {
        const cxpr_engine_column_source_def cols[] = {
            { "open",  &open_[0],  sizeof(double), NBARS },
            { "high",  &high_[0],  sizeof(double), NBARS },
            { "low",   &low_[0],   sizeof(double), NBARS },
            { "close", &close_[0], sizeof(double), NBARS },
        };
        const cxpr_engine_watch_def w[] = {
            { "buy", CXPR_EDGE_RISING }, { "exit", CXPR_EDGE_RISING },
        };
        cxpr_engine_config cfg = {0};
        cfg.expressions = EXPRS; cfg.expression_count = NEXPR;
        cfg.column_sources = cols; cfg.column_source_count = 4;
        cfg.watches = w; cfg.watch_count = 2;

        cxpr_engine_program* prog = cxpr_engine_program_new(&cfg, &err);
        if (!prog) { fprintf(stderr, "engine build failed: %s\n", err.message ? err.message : "?"); return 1; }

        /* Session built once (like the low-level evaluator); reset between reps so
         * the timed region is pure per-tick cost. */
        cxpr_engine_session* sess = cxpr_engine_session_new(prog);
        long long start = now_ns();
        for (r = 0; r < REPS; ++r) {
            cxpr_engine_session_reset(sess);
            for (i = 0; i < NBARS; ++i) {
                const cxpr_engine_event* ev2; size_t n; bool f;
                cxpr_engine_tick(sess, &ev2, &n, &err);
                eng_signals += (long long)n;
                g_sink += cxpr_engine_get_double(sess, "score", &f);
            }
        }
        eng_ns = now_ns() - start;
        cxpr_engine_session_free(sess);
        cxpr_engine_program_free(prog);
    }

    double total = (double)NBARS * (double)REPS;
    printf("bars=%u reps=%u  (%.0f ticks)\n", NBARS, REPS, total);
    printf("low-level: %8.2f ns/bar   signals=%lld\n", (double)low_ns / total, low_signals);
    printf("engine:    %8.2f ns/bar   signals=%lld\n", (double)eng_ns / total, eng_signals);
    printf("ratio engine/low-level: %.2fx\n", (double)eng_ns / (double)low_ns);
    printf("signal parity: %s\n", low_signals == eng_signals ? "OK" : "MISMATCH");
    printf("(sink %.3f)\n", (double)g_sink);

    return low_signals == eng_signals ? 0 : 2;
}
