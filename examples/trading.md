# Trading Example

Related test: [`../tests/examples/trading.test.c`](../tests/examples/trading.test.c)

This example shows how the expression evaluator can compose intermediate signals and evaluate them in dependency order. The snippet below includes the context values and parameters the expressions depend on.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "close",         98.5);
    cxpr_context_set(ctx, "ema_fast",      99.2);
    cxpr_context_set(ctx, "ema_slow",      97.8);
    cxpr_context_set(ctx, "rsi",           54.0);
    cxpr_context_set(ctx, "volume",   1500000.0);
    cxpr_context_set(ctx, "lower_band",    99.0);
    cxpr_context_set(ctx, "atr",            2.1);
    cxpr_context_set(ctx, "prev_ema_fast", 99.6);
    cxpr_context_set(ctx, "prev_ema_slow", 99.4);

    cxpr_context_set_param(ctx, "min_volume",    1000000.0);
    cxpr_context_set_param(ctx, "max_vol_ratio",       0.03);

    const cxpr_expression_def defs[] = {
        { "trend",    "close > ema_fast and ema_fast > ema_slow" },
        { "pullback", "close < ema_fast * 0.995" },
        { "entry",    "trend and pullback and rsi > 50" }
    };

    cxpr_expressions_add(evaluator, defs, 3, &err);
    cxpr_evaluator_compile(evaluator, &err);
    cxpr_evaluator_eval(evaluator, ctx, &err);

    printf("entry=%d\n", cxpr_expression_get_bool(evaluator, "entry", NULL));

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    return 0;
}
```

Direct expressions can stay inline when no dependency graph is needed. If you reference custom functions, register them first:

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

static cxpr_value fn_cross_below(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[2] >= args[3] && args[0] < args[1]);
}

int main(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};

    cxpr_register_builtins(reg);
    cxpr_registry_add_value(reg, "cross_below", fn_cross_below, 4, 4, NULL, NULL);

    cxpr_context_set(ctx, "rsi",           28.0);
    cxpr_context_set(ctx, "close",         98.5);
    cxpr_context_set(ctx, "lower_band",    99.0);
    cxpr_context_set(ctx, "volume",   1500000.0);
    cxpr_context_set(ctx, "ema_fast",      99.2);
    cxpr_context_set(ctx, "ema_slow",      98.8);
    cxpr_context_set(ctx, "prev_ema_fast", 99.6);
    cxpr_context_set(ctx, "prev_ema_slow", 99.4);
    cxpr_context_set(ctx, "atr",            2.1);
    cxpr_context_set_param(ctx, "min_volume",    1000000.0);
    cxpr_context_set_param(ctx, "max_vol_ratio",       0.03);

    cxpr_ast* ast = cxpr_parse(
        parser,
        "cross_below(ema_fast, ema_slow, prev_ema_fast, prev_ema_slow) or atr / close > $max_vol_ratio",
        &err
    );

    printf("signal=%d\n", cxpr_ast_eval_bool(ast, ctx, reg, &err));

    cxpr_ast_free(ast);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_parser_free(parser);
    return 0;
}
```

```text
rsi < 30 and close < lower_band and volume > $min_volume
cross_below(ema_fast, ema_slow, prev_ema_fast, prev_ema_slow) or atr / close > $max_vol_ratio
```

## Bar Loop

For bar-by-bar evaluation you normally reuse the same `cxpr_context` and overwrite the same keys on each iteration. You do not need a separate flush step for that.

```c
#include <cxpr/cxpr.h>
#include <stdio.h>

typedef struct {
    double close;
    double ema_fast;
    double ema_slow;
    double rsi;
    double volume;
} bar_t;

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    const bar_t bars[] = {
        {100.0,  99.0,  98.0, 55.0, 1200000.0},
        { 98.0,  99.0,  98.2, 58.0, 1400000.0},
        { 97.0,  98.5,  98.1, 45.0,  800000.0}
    };

    const cxpr_expression_def defs[] = {
        { "trend",    "close > ema_fast and ema_fast > ema_slow" },
        { "pullback", "close < ema_fast * 0.995" },
        { "entry",    "trend and pullback and rsi > 50 and volume > $min_volume" }
    };

    cxpr_register_builtins(reg);
    cxpr_context_set_param(ctx, "min_volume", 1000000.0);
    cxpr_expressions_add(evaluator, defs, 3, &err);
    cxpr_evaluator_compile(evaluator, &err);

    for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i) {
        cxpr_context_set(ctx, "close", bars[i].close);
        cxpr_context_set(ctx, "ema_fast", bars[i].ema_fast);
        cxpr_context_set(ctx, "ema_slow", bars[i].ema_slow);
        cxpr_context_set(ctx, "rsi", bars[i].rsi);
        cxpr_context_set(ctx, "volume", bars[i].volume);

        cxpr_evaluator_eval(evaluator, ctx, &err);
        printf("bar %zu entry=%d\n", i, cxpr_expression_get_bool(evaluator, "entry", NULL));
    }

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    return 0;
}
```

Use `cxpr_context_clear(ctx)` only when the set of keys changes between iterations and you want to guarantee that values from the previous bar cannot leak into the next one. If the same variables are written every time, overwriting them is enough.

## Run Test

From `libs/cxpr/`:

```bash
cmake --build build --target test_examples_trading
./build/tests/test_examples_trading
```
