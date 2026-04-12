#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include <cxpr/cxpr.h>

static cxpr_value fn_cross_below(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[2] >= args[3] && args[0] < args[1]);
}

int main(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    cxpr_register_defaults(reg);
    cxpr_registry_add_value(reg, "cross_below", fn_cross_below, 4, 4, NULL, NULL);

    cxpr_context_set(ctx, "close", 98.5);
    cxpr_context_set(ctx, "ema_fast", 99.2);
    cxpr_context_set(ctx, "ema_slow", 99.4);
    cxpr_context_set(ctx, "rsi", 54.0);
    cxpr_context_set(ctx, "volume", 1500000.0);
    cxpr_context_set(ctx, "lower_band", 99.0);
    cxpr_context_set(ctx, "atr", 2.1);
    cxpr_context_set(ctx, "prev_ema_fast", 99.6);
    cxpr_context_set(ctx, "prev_ema_slow", 99.4);

    cxpr_context_set_param(ctx, "min_volume", 1000000.0);
    cxpr_context_set_param(ctx, "max_vol_ratio", 0.03);

    const cxpr_expression_def defs[] = {
        { "trend", "close > ema_fast and ema_fast > ema_slow" },
        { "pullback", "close < ema_fast * 0.995" },
        { "entry", "trend and pullback and rsi > 50" }
    };

    assert(cxpr_expressions_add(evaluator, defs, 3, &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_bool(evaluator, "entry", NULL) == false);

    cxpr_ast* ast = cxpr_parse(
        parser,
        "cross_below(ema_fast, ema_slow, prev_ema_fast, prev_ema_slow) or atr / close > $max_vol_ratio",
        &err
    );
    assert(ast);
    {
        bool signal = false;
        const bool ok = cxpr_eval_ast_bool(ast, ctx, reg, &signal, &err);
        if (!ok) {
            fprintf(stderr, "trading bool eval failed: code=%d msg=%s\n",
                    (int)err.code, err.message ? err.message : "(null)");
        }
        assert(ok);
        assert(signal == true);
    }

    {
        typedef struct {
            double close;
            double ema_fast;
            double ema_slow;
            double rsi;
            double volume;
            bool expected_entry;
        } bar_t;

        const bar_t bars[] = {
            {100.0, 99.0, 98.0, 55.0, 1200000.0, false},
            { 98.0, 99.0, 98.2, 58.0, 1400000.0, false},
            { 97.0, 98.5, 98.1, 45.0,  800000.0, false}
        };

        const cxpr_expression_def loop_defs[] = {
            { "trend",    "close > ema_fast and ema_fast > ema_slow" },
            { "pullback", "close < ema_fast * 0.995" },
            { "entry",    "trend and pullback and rsi > 50 and volume > $min_volume" }
        };

        cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
        assert(evaluator);
        assert(cxpr_expressions_add(evaluator, loop_defs, 3, &err));
        assert(cxpr_evaluator_compile(evaluator, &err));

        for (size_t i = 0; i < sizeof(bars) / sizeof(bars[0]); ++i) {
            cxpr_context_set(ctx, "close", bars[i].close);
            cxpr_context_set(ctx, "ema_fast", bars[i].ema_fast);
            cxpr_context_set(ctx, "ema_slow", bars[i].ema_slow);
            cxpr_context_set(ctx, "rsi", bars[i].rsi);
            cxpr_context_set(ctx, "volume", bars[i].volume);

            cxpr_evaluator_eval(evaluator, ctx, &err);
            assert(err.code == CXPR_OK);
            assert(cxpr_expression_get_bool(evaluator, "entry", NULL) == bars[i].expected_entry);
        }

        cxpr_context_clear(ctx);
        err = (cxpr_error){0};
        cxpr_evaluator_eval(evaluator, ctx, &err);
        assert(err.code != CXPR_OK);

        cxpr_evaluator_free(evaluator);
    }

    cxpr_ast_free(ast);
    cxpr_evaluator_free(evaluator);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);

    printf("  \342\234\223 trading example\n");
    return 0;
}
