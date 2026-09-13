/* Benchmark: hand-rolled low-level evaluator loop vs the engine layer.
 *
 * Two scenarios over the same OHLCV bars:
 *   1. basic     - per-bar expressions, no lookback.
 *   2. lookback  - expressions with close[3]/high[1] plus a registered fn.
 *
 * The low-level path mirrors how a host drives the evaluator by hand. For
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

static void sink_add(double value) {
    if (isfinite(value)) g_sink += value;
}

static long long now_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double bench_decay(double x) { return x * 0.99; }

static void report(const char* name, long long low_ns, long long low_sig,
                   long long eng_ns, long long eng_sig) {
    double total = (double)NBARS * (double)REPS;
    printf("[%s] low-level %7.2f ns/bar | engine %7.2f ns/bar | engine/low %.2fx | parity %s\n",
           name, (double)low_ns / total, (double)eng_ns / total,
           (double)eng_ns / (double)low_ns, low_sig == eng_sig ? "OK" : "MISMATCH");
}

static void report_detail(const char* name, long long ns) {
    double total = (double)NBARS * (double)REPS;
    printf("  %-24s %7.2f ns/bar\n", name, (double)ns / total);
}

static cxpr_expr_ast* parse_or_die(cxpr_expr_parser* parser, const char* expr, cxpr_error* err) {
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, expr, err);
    if (!ast) {
        fprintf(stderr, "parse failed for '%s': %s\n", expr, err->message ? err->message : "?");
        exit(1);
    }
    return ast;
}

static cxpr_expr_compiled* compile_or_die(cxpr_expr_ast* ast, const cxpr_registry* reg, cxpr_error* err) {
    cxpr_expr_compiled* program = cxpr_expr_compile(ast, reg, err);
    if (!program) {
        fprintf(stderr, "compile failed: %s\n", err->message ? err->message : "?");
        exit(1);
    }
    return program;
}

/* ---------------- scenario 1: basic (no lookback) ---------------- */
static int run_basic(void) {
    static const cxpr_expression_def E[] = {
        { "buy",  "close > open && (high - low) > 0.5" },
        { "exit", "close < open" },
        { "score","(close - open) / (high - low + 0.0001)" },
    };
    cxpr_error err = {0};
    long long raw_ns, no_watch_ns, low_ns, eng_ns;
    long long raw_sig = 0, no_watch_sig = 0, low_sig = 0, eng_sig = 0;
    unsigned i, r;

    { /* raw compiled programs */
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_context* ctx = cxpr_context_new();
        cxpr_expr_ast* buy_ast;
        cxpr_expr_ast* exit_ast;
        cxpr_expr_ast* score_ast;
        cxpr_expr_compiled* buy_prog;
        cxpr_expr_compiled* exit_prog;
        cxpr_expr_compiled* score_prog;
        long long start;
        cxpr_register_defaults(reg);
        buy_ast = parse_or_die(parser, "close > open && (high - low) > 0.5", &err);
        exit_ast = parse_or_die(parser, "close < open", &err);
        score_ast = parse_or_die(parser, "(close - open) / (high - low + 0.0001)", &err);
        buy_prog = compile_or_die(buy_ast, reg, &err);
        exit_prog = compile_or_die(exit_ast, reg, &err);
        score_prog = compile_or_die(score_ast, reg, &err);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pb = false, pe = false;
            for (i = 0; i < NBARS; ++i) {
                bool b = false, e = false;
                double score = 0.0;
                cxpr_context_set(ctx, "open", open_[i]);
                cxpr_context_set(ctx, "high", high_[i]);
                cxpr_context_set(ctx, "low", low_[i]);
                cxpr_context_set(ctx, "close", close_[i]);
                if (!cxpr_expr_compiled_eval_bool(buy_prog, ctx, reg, &b, &err) ||
                    !cxpr_expr_compiled_eval_bool(exit_prog, ctx, reg, &e, &err) ||
                    !cxpr_expr_compiled_eval_number(score_prog, ctx, reg, &score, &err)) {
                    fprintf(stderr, "basic raw eval failed: %s\n", err.message ? err.message : "?");
                    exit(1);
                }
                sink_add(score);
                if (b && !pb) ++raw_sig;
                if (e && !pe) ++raw_sig;
                pb = b; pe = e;
            }
        }
        raw_ns = now_ns() - start;
        cxpr_expr_compiled_free(score_prog); cxpr_expr_compiled_free(exit_prog); cxpr_expr_compiled_free(buy_prog);
        cxpr_expr_ast_free(score_ast); cxpr_expr_ast_free(exit_ast); cxpr_expr_ast_free(buy_ast);
        cxpr_context_free(ctx); cxpr_expr_parser_free(parser); cxpr_registry_free(reg);
    }
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
                sink_add(cxpr_expression_get_double(ev, "score", &f));
                if (b && !pb) ++low_sig;
                if (e && !pe) ++low_sig;
                pb = b; pe = e;
            }
        }
        low_ns = now_ns() - start;
        cxpr_context_free(ctx); cxpr_evaluator_free(ev); cxpr_registry_free(reg);
    }
    { /* engine without watches */
        const cxpr_engine_column_source_def cols[] = {
            { "open", &open_[0], sizeof(double), NBARS },
            { "high", &high_[0], sizeof(double), NBARS },
            { "low",  &low_[0],  sizeof(double), NBARS },
            { "close",&close_[0],sizeof(double), NBARS },
        };
        cxpr_engine_config cfg = {0};
        cxpr_engine_program* prog;
        cxpr_engine_session* s;
        long long start;
        cfg.expressions = E; cfg.expression_count = 3;
        cfg.column_sources = cols; cfg.column_source_count = 4;
        prog = cxpr_engine_program_new(&cfg, &err);
        s = cxpr_engine_session_new(prog);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pb = false, pe = false;
            cxpr_engine_session_reset(s);
            for (i = 0; i < NBARS; ++i) {
                const cxpr_engine_event* ev2; size_t n; bool f;
                bool b, e;
                cxpr_engine_tick(s, &ev2, &n, &err);
                b = cxpr_engine_get_bool(s, "buy", &f);
                e = cxpr_engine_get_bool(s, "exit", &f);
                sink_add(cxpr_engine_get_double(s, "score", &f));
                if (b && !pb) ++no_watch_sig;
                if (e && !pe) ++no_watch_sig;
                pb = b; pe = e;
            }
        }
        no_watch_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog);
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
                sink_add(cxpr_engine_get_double(s, "score", &f));
            }
        }
        eng_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog);
    }
    report("basic   ", low_ns, low_sig, eng_ns, eng_sig);
    report_detail("basic.raw_programs", raw_ns);
    report_detail("basic.evaluator", low_ns);
    report_detail("basic.engine_nowatch", no_watch_ns);
    report_detail("basic.engine_watch", eng_ns);
    return raw_sig == low_sig && low_sig == no_watch_sig && low_sig == eng_sig;
}

/* ---------------- scenario 2: lookback + registered fn ---------------- */
static int run_lookback(void) {
    static const cxpr_expression_def E[] = {
        { "brk", "close > high[1]" },         /* lookback */
        { "mom", "close - close[3]" },         /* deeper lookback */
        { "sm",  "decay(close)" },             /* registered fn */
    };
    cxpr_error err = {0};
    long long raw_ns, no_watch_ns, low_ns, eng_ns;
    long long raw_sig = 0, no_watch_sig = 0, low_sig = 0, eng_sig = 0;
    unsigned i, r;

    { /* raw compiled programs with column lookback resolver */
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_context* ctx = cxpr_context_new();
        cxpr_expr_ast* brk_ast;
        cxpr_expr_ast* mom_ast;
        cxpr_expr_ast* sm_ast;
        cxpr_expr_compiled* brk_prog;
        cxpr_expr_compiled* mom_prog;
        cxpr_expr_compiled* sm_prog;
        long long start;
        const cxpr_lookback_column llcols[] = {
            { "close", &close_[0], sizeof(double), NBARS },
            { "high",  &high_[0],  sizeof(double), NBARS },
        };
        cxpr_register_defaults(reg);
        cxpr_registry_add_unary(reg, "decay", bench_decay);
        cxpr_register_column_lookback(reg, llcols, 2, &g_ll_cursor);
        brk_ast = parse_or_die(parser, "close > high[1]", &err);
        mom_ast = parse_or_die(parser, "close - close[3]", &err);
        sm_ast = parse_or_die(parser, "decay(close)", &err);
        brk_prog = compile_or_die(brk_ast, reg, &err);
        mom_prog = compile_or_die(mom_ast, reg, &err);
        sm_prog = compile_or_die(sm_ast, reg, &err);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pbrk = false;
            for (i = 0; i < NBARS; ++i) {
                bool b = false;
                double mom = 0.0, sm = 0.0;
                g_ll_cursor = (int64_t)i;
                cxpr_context_set(ctx, "high", high_[i]);
                cxpr_context_set(ctx, "close", close_[i]);
                if (!cxpr_expr_compiled_eval_bool(brk_prog, ctx, reg, &b, &err) ||
                    !cxpr_expr_compiled_eval_number(mom_prog, ctx, reg, &mom, &err) ||
                    !cxpr_expr_compiled_eval_number(sm_prog, ctx, reg, &sm, &err)) {
                    fprintf(stderr, "lookback raw eval failed: %s\n", err.message ? err.message : "?");
                    exit(1);
                }
                sink_add(mom); sink_add(sm);
                if (b && !pbrk) ++raw_sig;
                pbrk = b;
            }
        }
        raw_ns = now_ns() - start;
        cxpr_expr_compiled_free(sm_prog); cxpr_expr_compiled_free(mom_prog); cxpr_expr_compiled_free(brk_prog);
        cxpr_expr_ast_free(sm_ast); cxpr_expr_ast_free(mom_ast); cxpr_expr_ast_free(brk_ast);
        cxpr_context_free(ctx); cxpr_expr_parser_free(parser); cxpr_registry_free(reg);
    }
    { /* low-level: host writes + installs its own lookback resolver, tracks cursor */
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_evaluator* ev;
        cxpr_context* ctx;
        long long start;
        const cxpr_lookback_column llcols[] = {
            { "close", &close_[0], sizeof(double), NBARS },
            { "high",  &high_[0],  sizeof(double), NBARS },
        };
        cxpr_register_defaults(reg);
        cxpr_registry_add_unary(reg, "decay", bench_decay);
        /* Built-in column lookback — no hand-written resolver needed. */
        cxpr_register_column_lookback(reg, llcols, 2, &g_ll_cursor);
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
                sink_add(cxpr_expression_get_double(ev, "mom", &f));
                sink_add(cxpr_expression_get_double(ev, "sm", &f));
                if (b && !pbrk) ++low_sig;
                pbrk = b;
            }
        }
        low_ns = now_ns() - start;
        cxpr_context_free(ctx); cxpr_evaluator_free(ev); cxpr_registry_free(reg);
    }
    { /* engine without watches */
        cxpr_registry* reg = cxpr_registry_new();
        const cxpr_engine_column_source_def cols[] = {
            { "high", &high_[0],  sizeof(double), NBARS },
            { "close",&close_[0], sizeof(double), NBARS },
        };
        cxpr_engine_config cfg = {0};
        cxpr_engine_program* prog;
        cxpr_engine_session* s;
        long long start;
        cxpr_register_defaults(reg);
        cxpr_registry_add_unary(reg, "decay", bench_decay);
        cfg.registry = reg;
        cfg.expressions = E; cfg.expression_count = 3;
        cfg.column_sources = cols; cfg.column_source_count = 2;
        prog = cxpr_engine_program_new(&cfg, &err);
        if (!prog) { fprintf(stderr, "lookback no-watch build: %s\n", err.message ? err.message : "?"); return 0; }
        s = cxpr_engine_session_new(prog);
        start = now_ns();
        for (r = 0; r < REPS; ++r) {
            bool pbrk = false;
            cxpr_engine_session_reset(s);
            for (i = 0; i < NBARS; ++i) {
                const cxpr_engine_event* ev2; size_t n; bool f;
                bool b;
                cxpr_engine_tick(s, &ev2, &n, &err);
                b = cxpr_engine_get_bool(s, "brk", &f);
                sink_add(cxpr_engine_get_double(s, "mom", &f));
                sink_add(cxpr_engine_get_double(s, "sm", &f));
                if (b && !pbrk) ++no_watch_sig;
                pbrk = b;
            }
        }
        no_watch_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog); cxpr_registry_free(reg);
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
                sink_add(cxpr_engine_get_double(s, "mom", &f));
                sink_add(cxpr_engine_get_double(s, "sm", &f));
            }
        }
        eng_ns = now_ns() - start;
        cxpr_engine_session_free(s); cxpr_engine_program_free(prog); cxpr_registry_free(reg);
    }
    report("lookback", low_ns, low_sig, eng_ns, eng_sig);
    report_detail("lookback.raw_programs", raw_ns);
    report_detail("lookback.evaluator", low_ns);
    report_detail("lookback.engine_nowatch", no_watch_ns);
    report_detail("lookback.engine_watch", eng_ns);
    return raw_sig == low_sig && low_sig == no_watch_sig && low_sig == eng_sig;
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
