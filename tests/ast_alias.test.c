#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_expands(const char* expr, const cxpr_alias* aliases, size_t count, const char* expected) {
    char* out = NULL;
    cxpr_error err = {0};

    if (cxpr_expand_aliases(expr, aliases, count, &out, &err) != 1) {
        fprintf(stderr, "expand failed for %s: %s at %zu\n", expr, err.message ? err.message : "unknown", err.position);
        assert(0);
    }
    assert(out != NULL);
    assert(strcmp(out, expected) == 0);
    free(out);
}

static void test_expands_identifier_aliases_in_cross_call(void) {
    const cxpr_alias aliases[] = {
        {.name = "ema_fast", .expression = "ema(close, $fast_period)"},
        {.name = "ema_slow", .expression = "ema(close, $slow_period)"},
    };

    assert_expands(
        "cross_above(ema_fast, ema_slow)",
        aliases,
        2u,
        "cross_above(ema(close, $fast_period), ema(close, $slow_period))");
}

static void test_expands_nested_aliases(void) {
    const cxpr_alias aliases[] = {
        {.name = "fast", .expression = "ema(close, 12)"},
        {.name = "rule", .expression = "fast > close"},
    };

    assert_expands("rule and volume > 0", aliases, 2u, "ema(close, 12) > close and volume > 0");
}

static void test_expands_alias_field_to_producer_access(void) {
    const cxpr_alias aliases[] = {
        {.name = "m", .expression = "macd(close, 12, 26, 9)"},
    };

    assert_expands("m.signal > m.signal[1]", aliases, 1u,
                   "macd(close, 12, 26, 9).signal > macd(close, 12, 26, 9).signal[1]");
}

static void test_expands_alias_chain_to_producer_access(void) {
    const cxpr_alias aliases[] = {
        {.name = "m", .expression = "macd(close, 12, 26, 9)"},
    };

    assert_expands("m.signal.hist > 0", aliases, 1u,
                   "macd(close, 12, 26, 9).signal.hist > 0");
}

static void test_expands_alias_field_lookback_to_producer_access(void) {
    const cxpr_alias aliases[] = {
        {.name = "p", .expression = "swing_pivots(2, 2, 40)"},
        {.name = "lvl", .expression = "fib_level(p.high[1], p.low[1], 0.618)"},
        {.name = "rule", .expression = "close > lvl"},
    };

    assert_expands(
        "rule",
        aliases,
        3u,
        "close > fib_level(swing_pivots(2, 2, 40).high[1], swing_pivots(2, 2, 40).low[1], 0.618)");
}

static void test_expands_fibonacci_style_alias_chain(void) {
    const cxpr_alias aliases[] = {
        {.name = "p", .expression = "swing_pivots($pivot_left, $pivot_right, $pivot_lookback)"},
        {.name = "trend_up", .expression = "close > ema(close, $ema_period)"},
        {.name = "trend_down", .expression = "close < ema(close, $ema_period)"},
        {.name = "lvl_382", .expression = "fib_level(p.high[1], p.low[1], 0.382)"},
        {.name = "lvl_500", .expression = "fib_level(p.high[1], p.low[1], 0.500)"},
        {.name = "lvl_618", .expression = "fib_level(p.high[1], p.low[1], 0.618)"},
        {.name = "lvl_786", .expression = "fib_level(p.high[1], p.low[1], 0.786)"},
        {.name = "near_382", .expression = "fib_near(p.high[1], p.low[1], close, 0.382, $fib_tol)"},
        {.name = "near_618", .expression = "fib_near(p.high[1], p.low[1], close, 0.618, $fib_tol)"},
        {.name = "golden_zone", .expression = "fib_zone(p.high[1], p.low[1], close, 0.500, 0.618)"},
        {.name = "deep_zone", .expression = "fib_zone(p.high[1], p.low[1], close, 0.618, 0.786)"},
        {.name = "ext_1272", .expression = "fib_extension(p.low[1], p.high[1], p.low, 1.272)"},
        {.name = "ext_1618", .expression = "fib_extension(p.low[1], p.high[1], p.low, 1.618)"},
        {.name = "in_retracement", .expression = "golden_zone or deep_zone"},
        {.name = "rsi_ok", .expression = "rsi(close, $rsi_period) > $rsi_min"},
        {.name = "candle_confirm", .expression = "hammer() or bullish_engulfing(bar, bar[1])"},
        {.name = "long_setup", .expression = "trend_up and in_retracement and rsi_ok and candle_confirm"},
    };
    char* out = NULL;
    cxpr_error err = {0};

    if (cxpr_expand_aliases("long_setup", aliases, sizeof(aliases) / sizeof(aliases[0]), &out, &err) != 1) {
        fprintf(stderr, "expand failed: %s at %zu\n", err.message ? err.message : "unknown", err.position);
        assert(0);
    }
    assert(out != NULL);
    free(out);
}

static void test_expands_lookback_identifier_call_arg(void) {
    const cxpr_alias aliases[] = {
        {.name = "candle_confirm", .expression = "hammer() or bullish_engulfing(bar, bar[1])"},
    };

    assert_expands(
        "candle_confirm",
        aliases,
        1u,
        "hammer() or bullish_engulfing(bar, bar[1])");
}

static void test_rejects_cycles(void) {
    const cxpr_alias aliases[] = {
        {.name = "a", .expression = "b + 1"},
        {.name = "b", .expression = "a + 1"},
    };
    char* out = NULL;
    cxpr_error err = {0};

    assert(cxpr_expand_aliases("a", aliases, 2u, &out, &err) == 0);
    assert(out == NULL);
    assert(err.message != NULL);
}

int main(void) {
    test_expands_identifier_aliases_in_cross_call();
    test_expands_nested_aliases();
    test_expands_alias_field_to_producer_access();
    test_expands_alias_chain_to_producer_access();
    test_expands_alias_field_lookback_to_producer_access();
    test_expands_lookback_identifier_call_arg();
    test_expands_fibonacci_style_alias_chain();
    test_rejects_cycles();
    printf("  \xE2\x9C\x93 ast_alias\n");
    return 0;
}
