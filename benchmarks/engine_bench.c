/* Benchmark: hand-rolled low-level evaluator loop vs the engine layer.
 *
 * Two scenarios over the same OHLCV bars:
 *   1. basic     - per-bar expressions, no lookback.
 *   2. lookback  - expressions with close[3]/high[1] plus a registered fn.
 *
 * The low-level path mirrors how a host (dyn) drives the evaluator by hand. For
 * the lookback scenario it must also install its own lookback resolver and track
 * the cursor — exactly the boilerplate the engine absorbs. Each scenario asserts
 * both paths detect the same number of RISING signals (engine-vs-low-level
 * parity cross-check) and prints ns/bar + the ratio. */
#include <cxpr/engine.h>
#include <cxpr/cxpr.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NBARS 4096u
#define REPS  3000u

static volatile double g_sink = 0.0;
static double open_[NBARS], high_[NBARS], low_[NBARS], close_[NBARS];
static int64_t g_ll_cursor; /* low-level lookback cursor (host-tracked by hand) */

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double bench_decay(double x) { return x * 0.99; }

/* Low-level lookback resolver: the host must write this and thread the cursor. */
static bool ll_lookback(const cxpr_ast* target, const cxpr_ast* index,
                        const cxpr_context* ctx, const cxpr_registry* reg,
                        void* ud, cxpr_value* out, cxpr_error* err) {
    const char* nm;
    const double* a;
    int64_t n, idx;
    (void)ctx; (void)reg; (void)ud; (void)err;
    if (!index || cxpr_ast_type(index) != CXPR_NODE_NUMBER) return false;
    if (!target || cxpr_ast_type(target) != CXPR_NODE_IDENTIFIER) return false;
    n = (int64_t)cxpr_ast_number_value(index);
    nm = cxpr_ast_identifier_name(target);
    a = strcmp(nm, "close") == 0 ? close_ :
        strcmp(nm, "high") == 0  ? high_  :
        strcmp(nm, "open") == 0  ? open_  :
        strcmp(nm, "low") == 0   ? low_   : NULL;
    idx = g_ll_cursor - n;
    *out = cxpr_num((a && idx >= 0 && idx < (int64_t)NBARS) ? a[idx] : NAN);
    return true;
}

static void report(const char* name, long long low_ns, long long low_sig,
                   long long eng_ns, long long eng_sig) {
    double total = (double)NBARS * (double)REPS;
    printf("[%s] low-level %7.2f ns/bar | engine %7.2f ns/bar | engine/low %.2fx | parity %s\n",
           name, (double)low_ns / total, (double)eng_ns / total,
           (double)eng_ns / (double)low_ns, low_sig == eng_sig ? "OK" : "MISMATCH");
}

/* ---------------- scenario 1: basic (no lookback) ---------------- */
static int run_basic(void) {
    static const cxpr_expression_def E[] = {
        { "buy",  "close > open && (high - low) > 0.5" },
        { "exit", "close < open" },
        { "score","(close - open) / (high - low + 0.0001)" },
    };
    cxpr_error err = {0};
    long long low_ns, eng_ns, low_sig = 0, eng_sig = 0;
    unsigned i, r;

    { /* low-level */
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_evaluator* ev;
        cxpr_context* ctx;
        long long start;
        cxpr_register_defaults(reg);
        ev = cxpr_evaluator_new(reg);
        cxpr_expressions_add(ev, E, 3, &err);
        cxpr_evaluator_compile(ev, &err);
        ctx = cxpr_context_new();
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pb = false, pe = false;
            for (i = 0; i < NBARS; ++i) {
                bool f;
                cxpr_context_set(ctx, "open", open_[i]);
                cxpr_context_set(ctx, "high", high_[i]);
                cxpr_context_set(ctx, "low", low_[i]);
                cxpr_context_set(ctx, "close", close_[i]);
                cxpr_evaluator_eval(ev, ctx, &err);
                bool b = cxpr_expression_get_bool(ev, "buy", &f);
                bool e = cxpr_expression_get_bool(ev, "exit", &f);
                g_sink += cxpr_expression_get_double(ev, "score", &f);
                if (b && !pb) ++low_sig;
                if (e && !pe) ++low_sig;
                pb = b; pe = e;
            }
        }
        low_ns = now_ns() - start;
        cxpr_context_free(ctx); cxpr_evaluator_free(ev); cxpr_registry_free(reg);
    }
    { /* engine */
        const cxpr_engine_column_source_def cols[] = {
            { "open", &open_[0], sizeof(double), NBARS },
            { "high", &high_[0], sizeof(double), NBARS },
            { "low",  &low_[0],  sizeof(double), NBARS },
            { "close",&close_[0],sizeof(double), NBARS },
        };
        const cxpr_engine_watch_def w[] = { { "buy", CXPR_EDGE_RISING }, { "exit", CXPR_EDGE_RISING } };
        cxpr_engine_config cfg = {0};
        cxpr_engine_program* prog;
        cxpr_engine_session* s;
        long long start;
        cfg.expressions = E; cfg.expression_count = 3;
        cfg.column_sources = cols; cfg.column_source_count = 4;
        cfg.watches = w; cfg.watch_count = 2;
        prog = cxpr_engine_program_new(&cfg, &err);
        s = cxpr_engine_session_new(prog);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            cxpr_engine_session_reset(s);
            for (i = 0; i < NBARS; ++i) {
                const cxpr_engine_event* ev2; size_t n; bool f;
                cxpr_engine_tick(s, &ev2, &n, &err);
                eng_sig += (long long)n;
                g_sink += cxpr_engine_get_double(s, "score", &f);
            }
        }
        eng_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog);
    }
    report("basic   ", low_ns, low_sig, eng_ns, eng_sig);
    return low_sig == eng_sig;
}

/* ---------------- scenario 2: lookback + registered fn ---------------- */
static int run_lookback(void) {
    static const cxpr_expression_def E[] = {
        { "brk", "close > high[1]" },         /* lookback */
        { "mom", "close - close[3]" },         /* deeper lookback */
        { "sm",  "decay(close)" },             /* registered fn */
    };
    cxpr_error err = {0};
    long long low_ns, eng_ns, low_sig = 0, eng_sig = 0;
    unsigned i, r;

    { /* low-level: host writes + installs its own lookback resolver, tracks cursor */
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_evaluator* ev;
        cxpr_context* ctx;
        long long start;
        cxpr_register_defaults(reg);
        cxpr_registry_add_unary(reg, "decay", bench_decay);
        cxpr_registry_set_lookback_resolver(reg, ll_lookback, NULL, NULL);
        ev = cxpr_evaluator_new(reg);
        cxpr_expressions_add(ev, E, 3, &err);
        cxpr_evaluator_compile(ev, &err);
        ctx = cxpr_context_new();
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pbrk = false;
            for (i = 0; i < NBARS; ++i) {
                bool f;
                g_ll_cursor = (int64_t)i;          /* host must track the cursor */
                cxpr_context_set(ctx, "high", high_[i]);
                cxpr_context_set(ctx, "close", close_[i]);
                cxpr_evaluator_eval(ev, ctx, &err);
                bool b = cxpr_expression_get_bool(ev, "brk", &f);
                g_sink += cxpr_expression_get_double(ev, "mom", &f);
                g_sink += cxpr_expression_get_double(ev, "sm", &f);
                if (b && !pbrk) ++low_sig;
                pbrk = b;
            }
        }
        low_ns = now_ns() - start;
        cxpr_context_free(ctx); cxpr_evaluator_free(ev); cxpr_registry_free(reg);
    }
    { /* engine: declare the lookback exprs; engine owns rings + resolver */
        cxpr_registry* reg = cxpr_registry_new();
        const cxpr_engine_column_source_def cols[] = {
            { "high", &high_[0],  sizeof(double), NBARS },
            { "close",&close_[0], sizeof(double), NBARS },
        };
        const cxpr_engine_watch_def w[] = { { "brk", CXPR_EDGE_RISING } };
        cxpr_engine_config cfg = {0};
        cxpr_engine_program* prog;
        cxpr_engine_session* s;
        long long start;
        cxpr_register_defaults(reg);
        cxpr_registry_add_unary(reg, "decay", bench_decay);
        cfg.registry = reg;
        cfg.expressions = E; cfg.expression_count = 3;
        cfg.column_sources = cols; cfg.column_source_count = 2;
        cfg.watches = w; cfg.watch_count = 1;
        prog = cxpr_engine_program_new(&cfg, &err);
        if (!prog) { fprintf(stderr, "lookback engine build: %s\n", err.message ? err.message : "?"); return 0; }
        s = cxpr_engine_session_new(prog);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            cxpr_engine_session_reset(s);
            for (i = 0; i < NBARS; ++i) {
                const cxpr_engine_event* ev2; size_t n; bool f;
                cxpr_engine_tick(s, &ev2, &n, &err);
                eng_sig += (long long)n;
                g_sink += cxpr_engine_get_double(s, "mom", &f);
                g_sink += cxpr_engine_get_double(s, "sm", &f);
            }
        }
        eng_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog); cxpr_registry_free(reg);
    }
    report("lookback", low_ns, low_sig, eng_ns, eng_sig);
    return low_sig == eng_sig;
}

int main(void) {
    unsigned i;
    uint64_t s = 0x9e3779b97f4a7c15ULL;
    int ok = 1;
    for (i = 0; i < NBARS; ++i) {
        double o, c;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        o = 100.0 + (double)((s >> 33) % 1000) / 100.0;
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        c = 100.0 + (double)((s >> 33) % 1000) / 100.0;
        open_[i] = o; close_[i] = c;
        high_[i] = (o > c ? o : c) + 0.75;
        low_[i]  = (o < c ? o : c) - 0.75;
    }
    printf("bars=%u reps=%u  (%.0f ticks/scenario)\n", NBARS, REPS, (double)NBARS * REPS);
    ok &= run_basic();
    ok &= run_lookback();
    printf("(sink %.3f)\n", (double)g_sink);
    return ok ? 0 : 2;
}
