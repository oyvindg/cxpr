/**
 * @file simulation.test.c
 * @brief OHLC bar simulation tests for cxpr.
 *
 * Simulates 200 synthetic OHLC bars and evaluates trading expressions
 * bar-by-bar, demonstrating how codegen-generated rule functions would
 * operate at runtime with live indicator data.
 *
 * Tests covered:
 * - Synthetic OHLC generation (deterministic random walk)
 * - SMA computation and expression evaluation
 * - EMA computation via custom function + expression
 * - RSI computation via custom function + expression
 * - Bollinger Bands (upper/middle/lower)
 * - Complex multi-indicator entry/exit signals
 * - Bar-by-bar signal counting and validation
 * - FormulaEngine with indicator dependency chains
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#define NUM_BARS    200
#define EPSILON     1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)
#define ASSERT_APPROX(a, b, eps) assert(fabs((a) - (b)) < (eps))

/* ═══════════════════════════════════════════════════════════════════════════
 * OHLC Bar structure
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    double open;
    double high;
    double low;
    double close;
    double volume;
} Bar;

/* ═══════════════════════════════════════════════════════════════════════════
 * Deterministic PRNG (xorshift32)
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t s_rng_state = 42;

static uint32_t xorshift32(void) {
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x;
    return x;
}

/** @brief Returns a deterministic random double in [lo, hi). */
static double rand_range(double lo, double hi) {
    return lo + (hi - lo) * ((double)(xorshift32() & 0xFFFFFF) / (double)0x1000000);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Synthetic OHLC generation
 * ═══════════════════════════════════════════════════════════════════════════ */

static void generate_bars(Bar* bars, size_t n) {
    double price = 100.0;

    for (size_t i = 0; i < n; i++) {
        double change_pct = rand_range(-0.03, 0.03); /* ±3% per bar */
        double open = price;
        double close = open * (1.0 + change_pct);

        double body_hi = (open > close) ? open : close;
        double body_lo = (open < close) ? open : close;

        double high = body_hi + rand_range(0.0, body_hi * 0.01);
        double low  = body_lo - rand_range(0.0, body_lo * 0.01);
        double volume = rand_range(1000.0, 10000.0);

        bars[i].open   = open;
        bars[i].high   = high;
        bars[i].low    = low;
        bars[i].close  = close;
        bars[i].volume = volume;

        price = close;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Indicator computation helpers (C reference implementations)
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief Compute SMA over the last `period` close values. */
static double compute_sma(const Bar* bars, size_t current, int period) {
    if ((int)current < period - 1) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < period; i++) {
        sum += bars[current - (size_t)i].close;
    }
    return sum / (double)period;
}

/** @brief Compute EMA given previous EMA and current close. */
static double compute_ema(double prev_ema, double close, int period) {
    double alpha = 2.0 / ((double)period + 1.0);
    return alpha * close + (1.0 - alpha) * prev_ema;
}

/** @brief Compute RSI over the last `period` bars. */
static double compute_rsi(const Bar* bars, size_t current, int period) {
    if ((int)current < period) return 50.0; /* neutral default */
    double gain_sum = 0.0, loss_sum = 0.0;
    for (int i = 0; i < period; i++) {
        double diff = bars[current - (size_t)i].close
                    - bars[current - (size_t)i - 1].close;
        if (diff > 0.0) gain_sum += diff;
        else            loss_sum -= diff; /* make positive */
    }
    double avg_gain = gain_sum / (double)period;
    double avg_loss = loss_sum / (double)period;
    if (avg_loss < EPSILON) return 100.0;
    double rs = avg_gain / avg_loss;
    return 100.0 - (100.0 / (1.0 + rs));
}

/** @brief Compute standard deviation of close over the last `period` bars. */
static double compute_stddev(const Bar* bars, size_t current, int period) {
    if ((int)current < period - 1) return 0.0;
    double mean = compute_sma(bars, current, period);
    double sum_sq = 0.0;
    for (int i = 0; i < period; i++) {
        double diff = bars[current - (size_t)i].close - mean;
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq / (double)period);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Custom cxpr functions for indicators
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief EMA step: ema_step(prev_ema, close, period) */
static double fn_ema_step(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    double prev = args[0], close = args[1], period = args[2];
    double alpha = 2.0 / (period + 1.0);
    return alpha * close + (1.0 - alpha) * prev;
}

/** @brief Cross above: cross_above(current_fast, current_slow, prev_fast, prev_slow) */
static double fn_cross_above(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    /* current_fast > current_slow AND prev_fast <= prev_slow */
    return (args[0] > args[1] && args[2] <= args[3]) ? 1.0 : 0.0;
}

/** @brief Cross below: cross_below(current_fast, current_slow, prev_fast, prev_slow) */
static double fn_cross_below(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    /* current_fast < current_slow AND prev_fast >= prev_slow */
    return (args[0] < args[1] && args[2] >= args[3]) ? 1.0 : 0.0;
}

/** @brief Percentage change: pct_change(current, previous) */
static double fn_pct_change(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    if (fabs(args[1]) < EPSILON) return 0.0;
    return (args[0] - args[1]) / fabs(args[1]) * 100.0;
}

/** @brief True range: true_range(high, low, prev_close) */
static double fn_true_range(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    double hl = args[0] - args[1];
    double hc = fabs(args[0] - args[2]);
    double lc = fabs(args[1] - args[2]);
    double m = hl;
    if (hc > m) m = hc;
    if (lc > m) m = lc;
    return m;
}

/** @brief Volatility ratio: vol_ratio(atr, close) = atr / close * 100 */
static double fn_vol_ratio(const double* args, size_t n, void* u) {
    (void)n; (void)u;
    if (fabs(args[1]) < EPSILON) return 0.0;
    return args[0] / args[1] * 100.0;
}

/** @brief Register all custom trading functions in the registry. */
static void register_trading_functions(cxpr_registry* reg) {
    cxpr_registry_add(reg, "ema_step",    fn_ema_step,    3, 3, NULL, NULL);
    cxpr_registry_add(reg, "cross_above", fn_cross_above, 4, 4, NULL, NULL);
    cxpr_registry_add(reg, "cross_below", fn_cross_below, 4, 4, NULL, NULL);
    cxpr_registry_add(reg, "pct_change",  fn_pct_change,  2, 2, NULL, NULL);
    cxpr_registry_add(reg, "true_range",  fn_true_range,  3, 3, NULL, NULL);
    cxpr_registry_add(reg, "vol_ratio",   fn_vol_ratio,   2, 2, NULL, NULL);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 1: SMA crossover strategy over 200 bars
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_sma_crossover(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 42; /* deterministic seed */
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    /* Pre-parse expressions (like codegen would) */
    cxpr_ast* entry_ast = cxpr_parse(p,
        "cross_above(sma_fast, sma_slow, prev_sma_fast, prev_sma_slow) and close > sma_slow",
        &err);
    assert(entry_ast);

    cxpr_ast* exit_ast = cxpr_parse(p,
        "cross_below(sma_fast, sma_slow, prev_sma_fast, prev_sma_slow) or close < sma_slow * 0.98",
        &err);
    assert(exit_ast);

    int sma_fast_period = 10;
    int sma_slow_period = 20;
    int entry_signals = 0, exit_signals = 0;
    double prev_sma_fast = 0.0, prev_sma_slow = 0.0;

    for (size_t i = 0; i < NUM_BARS; i++) {
        double sma_fast = compute_sma(bars, i, sma_fast_period);
        double sma_slow = compute_sma(bars, i, sma_slow_period);

        /* Set all bar data + indicators as context */
        cxpr_context_set(ctx, "open",  bars[i].open);
        cxpr_context_set(ctx, "high",  bars[i].high);
        cxpr_context_set(ctx, "low",   bars[i].low);
        cxpr_context_set(ctx, "close", bars[i].close);
        cxpr_context_set(ctx, "volume", bars[i].volume);
        cxpr_context_set(ctx, "sma_fast", sma_fast);
        cxpr_context_set(ctx, "sma_slow", sma_slow);
        cxpr_context_set(ctx, "prev_sma_fast", prev_sma_fast);
        cxpr_context_set(ctx, "prev_sma_slow", prev_sma_slow);

        /* Evaluate after warm-up period */
        if ((int)i >= sma_slow_period) {
            err.code = CXPR_OK;
            bool entry = cxpr_ast_eval_bool(entry_ast, ctx, reg, &err);
            assert(err.code == CXPR_OK);
            if (entry) entry_signals++;

            err.code = CXPR_OK;
            bool exit = cxpr_ast_eval_bool(exit_ast, ctx, reg, &err);
            assert(err.code == CXPR_OK);
            if (exit) exit_signals++;
        }

        prev_sma_fast = sma_fast;
        prev_sma_slow = sma_slow;
    }

    /* With 200 bars we expect some signals (not zero, not every bar) */
    assert(entry_signals > 0 && "Should have at least one entry signal");
    assert(exit_signals > 0 && "Should have at least one exit signal");
    assert(entry_signals < NUM_BARS / 2 && "Entry signals should be sparse");
    assert(exit_signals < NUM_BARS / 2 && "Exit signals should be sparse");

    cxpr_ast_free(entry_ast);
    cxpr_ast_free(exit_ast);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_sma_crossover (%d entries, %d exits over %d bars)\n",
           entry_signals, exit_signals, NUM_BARS);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 2: EMA convergence — verify expression matches reference impl
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_ema_convergence(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 123; /* different seed */
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    cxpr_ast* ema_expr = cxpr_parse(p, "ema_step(prev_ema, close, $period)", &err);
    assert(ema_expr);

    int period = 20;
    cxpr_context_set_param(ctx, "period", (double)period);

    /* Initialize EMA with first close */
    double ref_ema = bars[0].close;
    double expr_ema = bars[0].close;

    for (size_t i = 1; i < NUM_BARS; i++) {
        /* Reference implementation */
        ref_ema = compute_ema(ref_ema, bars[i].close, period);

        /* Expression-based implementation */
        cxpr_context_set(ctx, "prev_ema", expr_ema);
        cxpr_context_set(ctx, "close", bars[i].close);
        err.code = CXPR_OK;
        expr_ema = cxpr_ast_eval(ema_expr, ctx, reg, &err);
        assert(err.code == CXPR_OK);

        /* They should be bit-identical since the formula is the same */
        ASSERT_DOUBLE_EQ(expr_ema, ref_ema);
    }

    /* EMA should be close to final price (not stuck at initial) */
    double final_close = bars[NUM_BARS - 1].close;
    double pct_diff = fabs(expr_ema - final_close) / final_close * 100.0;
    assert(pct_diff < 15.0 && "EMA should track price within 15%");

    cxpr_ast_free(ema_expr);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_ema_convergence (EMA20 final=%.4f, close=%.4f, diff=%.2f%%)\n",
           expr_ema, final_close, pct_diff);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 3: RSI signal — expression evaluates RSI thresholds per bar
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_rsi_signals(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 777;
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    /* Parameterised RSI thresholds */
    cxpr_context_set_param(ctx, "oversold", 30.0);
    cxpr_context_set_param(ctx, "overbought", 70.0);

    cxpr_ast* oversold_ast = cxpr_parse(p, "rsi < $oversold", &err);
    assert(oversold_ast);

    cxpr_ast* overbought_ast = cxpr_parse(p, "rsi > $overbought", &err);
    assert(overbought_ast);

    /* Complex signal with RSI + price confirmation */
    cxpr_ast* buy_signal_ast = cxpr_parse(p,
        "rsi < $oversold and close > low and pct_change(close, prev_close) > 0",
        &err);
    assert(buy_signal_ast);

    int rsi_period = 14;
    int oversold_count = 0, overbought_count = 0, buy_signals = 0;

    for (size_t i = 0; i < NUM_BARS; i++) {
        double rsi = compute_rsi(bars, i, rsi_period);

        cxpr_context_set(ctx, "open",  bars[i].open);
        cxpr_context_set(ctx, "high",  bars[i].high);
        cxpr_context_set(ctx, "low",   bars[i].low);
        cxpr_context_set(ctx, "close", bars[i].close);
        cxpr_context_set(ctx, "rsi",   rsi);
        cxpr_context_set(ctx, "prev_close", (i > 0) ? bars[i - 1].close : bars[i].close);

        if ((int)i >= rsi_period) {
            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(oversold_ast, ctx, reg, &err))  oversold_count++;
            assert(err.code == CXPR_OK);

            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(overbought_ast, ctx, reg, &err)) overbought_count++;
            assert(err.code == CXPR_OK);

            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(buy_signal_ast, ctx, reg, &err)) buy_signals++;
            assert(err.code == CXPR_OK);
        }
    }

    /* RSI should hit extremes sometimes with 200 bars of random walk */
    assert(oversold_count + overbought_count > 0 &&
           "RSI should reach extremes with 200 bars");

    cxpr_ast_free(oversold_ast);
    cxpr_ast_free(overbought_ast);
    cxpr_ast_free(buy_signal_ast);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_rsi_signals (oversold=%d, overbought=%d, buy=%d over %d bars)\n",
           oversold_count, overbought_count, buy_signals, NUM_BARS);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 4: Bollinger Bands — upper/middle/lower computed and rule-evaluated
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_bollinger_bands(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 999;
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    int bb_period = 20;
    double bb_mult = 2.0;
    cxpr_context_set_param(ctx, "bb_mult", bb_mult);

    /* Bollinger squeeze: bandwidth < threshold */
    cxpr_ast* squeeze_ast = cxpr_parse(p,
        "(bb_upper - bb_lower) / bb_middle < $squeeze_thresh", &err);
    assert(squeeze_ast);
    cxpr_context_set_param(ctx, "squeeze_thresh", 0.06);

    /* Price near lower band: potential buy */
    cxpr_ast* near_lower_ast = cxpr_parse(p,
        "close < bb_lower + (bb_middle - bb_lower) * 0.2", &err);
    assert(near_lower_ast);

    /* Mean reversion: price crosses above middle band */
    cxpr_ast* mean_reversion_ast = cxpr_parse(p,
        "close > bb_middle and prev_close <= bb_middle and rsi < 60", &err);
    assert(mean_reversion_ast);

    int squeeze_count = 0, near_lower_count = 0, mean_rev_count = 0;
    int rsi_period = 14;

    for (size_t i = 0; i < NUM_BARS; i++) {
        double sma = compute_sma(bars, i, bb_period);
        double stddev = compute_stddev(bars, i, bb_period);
        double upper = sma + bb_mult * stddev;
        double lower = sma - bb_mult * stddev;
        double rsi = compute_rsi(bars, i, rsi_period);

        cxpr_context_set(ctx, "close",     bars[i].close);
        cxpr_context_set(ctx, "prev_close", (i > 0) ? bars[i - 1].close : bars[i].close);
        cxpr_context_set(ctx, "bb_upper",  upper);
        cxpr_context_set(ctx, "bb_middle", sma);
        cxpr_context_set(ctx, "bb_lower",  lower);
        cxpr_context_set(ctx, "rsi",       rsi);

        if ((int)i >= bb_period) {
            /* Verify bands are correctly ordered */
            assert(upper >= sma && "Upper band must be >= middle");
            assert(lower <= sma && "Lower band must be <= middle");

            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(squeeze_ast, ctx, reg, &err)) squeeze_count++;
            assert(err.code == CXPR_OK);

            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(near_lower_ast, ctx, reg, &err)) near_lower_count++;
            assert(err.code == CXPR_OK);

            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(mean_reversion_ast, ctx, reg, &err)) mean_rev_count++;
            assert(err.code == CXPR_OK);
        }
    }

    cxpr_ast_free(squeeze_ast);
    cxpr_ast_free(near_lower_ast);
    cxpr_ast_free(mean_reversion_ast);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_bollinger_bands (squeeze=%d, near_lower=%d, mean_rev=%d)\n",
           squeeze_count, near_lower_count, mean_rev_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 5: Full multi-indicator strategy — EMA + RSI + Bollinger + ATR
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_full_strategy_simulation(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 31337;
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    /* Strategy parameters */
    cxpr_context_set_param(ctx, "ema_fast_period", 12.0);
    cxpr_context_set_param(ctx, "ema_slow_period", 26.0);
    cxpr_context_set_param(ctx, "rsi_oversold", 35.0);
    cxpr_context_set_param(ctx, "rsi_overbought", 65.0);
    cxpr_context_set_param(ctx, "vol_max", 5.0);

    /* Complex entry: EMA bullish + RSI not overbought + volatility ok + near lower BB */
    cxpr_ast* entry_ast = cxpr_parse(p,
        "ema_fast > ema_slow and rsi > $rsi_oversold and rsi < $rsi_overbought and vol_ratio(atr, close) < $vol_max and close > bb_lower",
        &err);
    assert(entry_ast);

    /* Complex exit: EMA bearish OR RSI extreme OR volatility spike */
    cxpr_ast* exit_ast = cxpr_parse(p,
        "ema_fast < ema_slow or rsi > $rsi_overbought or vol_ratio(atr, close) > $vol_max * 2",
        &err);
    assert(exit_ast);

    /* Position sizing expression: normalize risk based on ATR */
    cxpr_ast* size_ast = cxpr_parse(p,
        "clamp(floor(10000 / (atr * 2)), 1, 100)",
        &err);
    assert(size_ast);

    /* Stop loss expression */
    cxpr_ast* stoploss_ast = cxpr_parse(p,
        "close - atr * 1.5",
        &err);
    assert(stoploss_ast);

    /* Indicator state */
    double ema_fast = bars[0].close;
    double ema_slow = bars[0].close;
    double atr = 0.0;
    int bb_period = 20;
    int rsi_period = 14;
    int atr_period = 14;

    int entries = 0, exits = 0;
    bool in_position = false;
    double total_pnl = 0.0;
    double entry_price = 0.0;

    for (size_t i = 1; i < NUM_BARS; i++) {
        /* Update indicators */
        ema_fast = compute_ema(ema_fast, bars[i].close, 12);
        ema_slow = compute_ema(ema_slow, bars[i].close, 26);
        double rsi = compute_rsi(bars, i, rsi_period);
        double sma = compute_sma(bars, i, bb_period);
        double stddev = compute_stddev(bars, i, bb_period);
        double bb_upper = sma + 2.0 * stddev;
        double bb_lower = sma - 2.0 * stddev;

        /* True range for ATR */
        double tr = bars[i].high - bars[i].low;
        if (i > 0) {
            double hc = fabs(bars[i].high - bars[i - 1].close);
            double lc = fabs(bars[i].low - bars[i - 1].close);
            if (hc > tr) tr = hc;
            if (lc > tr) tr = lc;
        }
        /* Exponential ATR */
        if (i == 1) atr = tr;
        else atr = ((double)(atr_period - 1) * atr + tr) / (double)atr_period;

        /* Load all context */
        cxpr_context_set(ctx, "open",     bars[i].open);
        cxpr_context_set(ctx, "high",     bars[i].high);
        cxpr_context_set(ctx, "low",      bars[i].low);
        cxpr_context_set(ctx, "close",    bars[i].close);
        cxpr_context_set(ctx, "volume",   bars[i].volume);
        cxpr_context_set(ctx, "ema_fast", ema_fast);
        cxpr_context_set(ctx, "ema_slow", ema_slow);
        cxpr_context_set(ctx, "rsi",      rsi);
        cxpr_context_set(ctx, "atr",      atr);
        cxpr_context_set(ctx, "bb_upper", bb_upper);
        cxpr_context_set(ctx, "bb_middle", sma);
        cxpr_context_set(ctx, "bb_lower", bb_lower);

        /* Skip warm-up */
        if ((int)i < bb_period + rsi_period) continue;

        if (!in_position) {
            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(entry_ast, ctx, reg, &err)) {
                assert(err.code == CXPR_OK);

                /* Compute position size */
                err.code = CXPR_OK;
                double size = cxpr_ast_eval(size_ast, ctx, reg, &err);
                assert(err.code == CXPR_OK);
                assert(size >= 1.0 && size <= 100.0);

                /* Compute stop loss */
                err.code = CXPR_OK;
                double sl = cxpr_ast_eval(stoploss_ast, ctx, reg, &err);
                assert(err.code == CXPR_OK);
                assert(sl < bars[i].close && "Stop loss must be below entry");

                entry_price = bars[i].close;
                in_position = true;
                entries++;
            }
        } else {
            err.code = CXPR_OK;
            if (cxpr_ast_eval_bool(exit_ast, ctx, reg, &err)) {
                assert(err.code == CXPR_OK);
                total_pnl += bars[i].close - entry_price;
                in_position = false;
                exits++;
            }
        }
    }

    /* Close any open position at the end */
    if (in_position) {
        total_pnl += bars[NUM_BARS - 1].close - entry_price;
        exits++;
    }

    /* Should have traded at least once */
    assert(entries > 0 && "Strategy should generate at least one trade");
    assert(entries == exits && "Every entry should have an exit");

    cxpr_ast_free(entry_ast);
    cxpr_ast_free(exit_ast);
    cxpr_ast_free(size_ast);
    cxpr_ast_free(stoploss_ast);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_full_strategy_simulation (%d trades, PnL=%.4f)\n",
           entries, total_pnl);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 6: FormulaEngine — chained indicator dependency resolution
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_formula_engine_indicators(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    assert(engine);

    /* Define a chain of formulas that depend on each other:
       atr_norm depends on atr and close
       risk_score depends on atr_norm and rsi
       position_size depends on risk_score
       signal depends on everything */
    assert(cxpr_formula_add(engine, "atr_norm", "atr / close * 100", &err));
    assert(cxpr_formula_add(engine, "risk_score",
        "clamp(atr_norm * 10 + abs(50 - rsi), 0, 100)", &err));
    assert(cxpr_formula_add(engine, "position_size",
        "floor(max(1, 100 - risk_score))", &err));
    assert(cxpr_formula_add(engine, "signal",
        "position_size > 50 and rsi < 60 and atr_norm < 3", &err));

    /* Compile (resolves dependencies) */
    assert(cxpr_formula_compile(engine, &err));
    assert(err.code == CXPR_OK);

    /* Verify evaluation order respects dependencies */
    const char* order[16];
    size_t n = cxpr_formula_eval_order(engine, order, 16);
    assert(n == 4);

    /* atr_norm must come before risk_score, which must come before position_size,
       which must come before signal */
    int idx_atr_norm = -1, idx_risk = -1, idx_pos = -1, idx_sig = -1;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(order[i], "atr_norm") == 0)      idx_atr_norm = (int)i;
        if (strcmp(order[i], "risk_score") == 0)     idx_risk = (int)i;
        if (strcmp(order[i], "position_size") == 0)  idx_pos = (int)i;
        if (strcmp(order[i], "signal") == 0)          idx_sig = (int)i;
    }
    assert(idx_atr_norm >= 0 && idx_risk >= 0 && idx_pos >= 0 && idx_sig >= 0);
    assert(idx_atr_norm < idx_risk && "atr_norm before risk_score");
    assert(idx_risk < idx_pos && "risk_score before position_size");
    assert(idx_pos < idx_sig && "position_size before signal");

    /* Now simulate bar-by-bar evaluation using the formula engine */
    Bar bars[NUM_BARS];
    s_rng_state = 54321;
    generate_bars(bars, NUM_BARS);

    double atr = 1.0;
    int signal_count = 0;

    for (size_t i = 1; i < NUM_BARS; i++) {
        double tr = bars[i].high - bars[i].low;
        atr = (13.0 * atr + tr) / 14.0;
        double rsi = compute_rsi(bars, i, 14);

        /* Set base inputs */
        cxpr_context_set(ctx, "close", bars[i].close);
        cxpr_context_set(ctx, "atr",   atr);
        cxpr_context_set(ctx, "rsi",   rsi);

        /* Evaluate formulas in dependency order */
        err.code = CXPR_OK;
        cxpr_formula_eval_all(engine, ctx, &err);
        assert(err.code == CXPR_OK);

        /* Read computed values back from context */
        bool found = false;
        double atr_norm = cxpr_context_get(ctx, "atr_norm", &found);
        assert(found);
        assert(atr_norm >= 0.0); /* normalized ATR should be positive */

        double risk = cxpr_context_get(ctx, "risk_score", &found);
        assert(found);
        assert(risk >= 0.0 && risk <= 100.0); /* clamped */

        double pos = cxpr_context_get(ctx, "position_size", &found);
        assert(found);
        assert(pos >= 1.0 && pos <= 100.0);

        double sig = cxpr_context_get(ctx, "signal", &found);
        assert(found);
        if (sig != 0.0) signal_count++;
    }

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_formula_engine_indicators (%d signals from formula chain)\n",
           signal_count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test 7: Stress test — 200 bars × multiple complex expressions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_stress_multi_expression(void) {
    Bar bars[NUM_BARS];
    s_rng_state = 2025;
    generate_bars(bars, NUM_BARS);

    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    register_trading_functions(reg);

    /* Pre-parse 8 different expressions */
    const char* expressions[] = {
        /* 0: trend following */
        "ema_fast > ema_slow and close > ema_fast and rsi > 50",
        /* 1: mean reversion */
        "close < bb_lower and rsi < 30",
        /* 2: breakout */
        "close > bb_upper and vol_ratio(atr, close) > 2",
        /* 3: momentum */
        "pct_change(close, prev_close) > 1.0 and rsi > 60 and rsi < 80",
        /* 4: risk sizing */
        "clamp(floor(10000 / (atr * 3)), 1, 200)",
        /* 5: composite score */
        "if(ema_fast > ema_slow, 1, 0) + if(rsi < 70, 1, 0) + if(close > bb_middle, 1, 0)",
        /* 6: volatility regime */
        "vol_ratio(atr, close) < 1.5 ? 1.0 : (vol_ratio(atr, close) < 3.0 ? 0.5 : 0.0)",
        /* 7: everything kitchen sink */
        "((ema_fast > ema_slow and not (rsi > 80)) or (close < bb_lower and rsi < 35)) and vol_ratio(atr, close) < 4 and abs(pct_change(close, prev_close)) < 5",
    };
    int n_expr = 8;

    cxpr_ast* asts[8];
    for (int i = 0; i < n_expr; i++) {
        asts[i] = cxpr_parse(p, expressions[i], &err);
        assert(asts[i] && "All expressions must parse");
    }

    /* Run all 8 expressions over 200 bars */
    double ema_fast = bars[0].close, ema_slow = bars[0].close;
    double atr = 0.0;
    int eval_count = 0;

    for (size_t i = 1; i < NUM_BARS; i++) {
        ema_fast = compute_ema(ema_fast, bars[i].close, 12);
        ema_slow = compute_ema(ema_slow, bars[i].close, 26);
        double rsi = compute_rsi(bars, i, 14);
        double sma = compute_sma(bars, i, 20);
        double std = compute_stddev(bars, i, 20);
        double tr = bars[i].high - bars[i].low;
        if (i == 1) atr = tr;
        else atr = (13.0 * atr + tr) / 14.0;

        cxpr_context_set(ctx, "close", bars[i].close);
        cxpr_context_set(ctx, "prev_close", bars[i - 1].close);
        cxpr_context_set(ctx, "ema_fast", ema_fast);
        cxpr_context_set(ctx, "ema_slow", ema_slow);
        cxpr_context_set(ctx, "rsi", rsi);
        cxpr_context_set(ctx, "atr", atr);
        cxpr_context_set(ctx, "bb_upper", sma + 2.0 * std);
        cxpr_context_set(ctx, "bb_middle", sma);
        cxpr_context_set(ctx, "bb_lower", sma - 2.0 * std);

        if ((int)i < 30) continue; /* warm-up */

        for (int j = 0; j < n_expr; j++) {
            err.code = CXPR_OK;
            double result = cxpr_ast_eval(asts[j], ctx, reg, &err);
            assert(err.code == CXPR_OK);
            assert(!isnan(result) && "Result must not be NaN");
            eval_count++;
        }
    }

    for (int i = 0; i < n_expr; i++) {
        cxpr_ast_free(asts[i]);
    }

    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_stress_multi_expression (%d evaluations across %d expressions × %d bars)\n",
           eval_count, n_expr, NUM_BARS);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running OHLC simulation tests (%d bars)...\n", NUM_BARS);
    test_sma_crossover();
    test_ema_convergence();
    test_rsi_signals();
    test_bollinger_bands();
    test_full_strategy_simulation();
    test_formula_engine_indicators();
    test_stress_multi_expression();
    printf("All simulation tests passed!\n");
    return 0;
}
