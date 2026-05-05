#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_expands(const char* expr, const cxpr_alias* aliases, size_t count, const char* expected) {
    char* out = NULL;
    cxpr_error err = {0};

    assert(cxpr_expand_aliases(expr, aliases, count, &out, &err) == 1);
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
    test_rejects_cycles();
    printf("  \xE2\x9C\x93 ast_alias\n");
    return 0;
}
