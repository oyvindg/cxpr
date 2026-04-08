/**
 * @file timeseries.c
 * @brief Native time-series helpers and builtins for cxpr.
 */

#include "internal.h"
#include <math.h>

typedef enum {
    CXPR_TIMESERIES_TREND_RISING = 1,
    CXPR_TIMESERIES_TREND_FALLING = -1
} cxpr_timeseries_trend_mode;

/**
 * @brief Shared evaluator for monotonic trend predicates over historical offsets.
 * @param call_ast Function-call AST containing `(value_expr, bars)`.
 * @param ctx Runtime context.
 * @param reg Function registry.
 * @param mode Direction to test.
 * @param err Optional error output.
 * @return Boolean value indicating whether the series is strictly monotonic.
 */
static cxpr_value cxpr_timeseries_trend_eval(const cxpr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             cxpr_timeseries_trend_mode mode,
                                             cxpr_error* err) {
    const cxpr_ast* value_ast;
    const cxpr_ast* bars_ast;
    double bars_value = 0.0;
    long long bars_ll;

    if (!call_ast || cxpr_ast_type(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return cxpr_fv_bool(false);
    }

    if (cxpr_ast_function_argc(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = mode == CXPR_TIMESERIES_TREND_RISING
                ? "rising(...) expects value, bars"
                : "falling(...) expects value, bars";
        }
        return cxpr_fv_bool(false);
    }

    value_ast = cxpr_ast_function_arg(call_ast, 0);
    bars_ast = cxpr_ast_function_arg(call_ast, 1);

    if (!cxpr_eval_ast_number(bars_ast, ctx, reg, &bars_value, err)) {
        return cxpr_fv_bool(false);
    }

    bars_ll = (long long)llround(bars_value);
    if (!isfinite(bars_value) || fabs(bars_value - (double)bars_ll) > 1e-9 || bars_ll < 2) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = mode == CXPR_TIMESERIES_TREND_RISING
                ? "rising(...) requires integer bars >= 2"
                : "falling(...) requires integer bars >= 2";
        }
        return cxpr_fv_bool(false);
    }

    for (long long i = 0; i < bars_ll - 1; ++i) {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &lhs, err) ||
            !cxpr_eval_ast_number_at_offset(value_ast, (double)(i + 1), ctx, reg, &rhs, err)) {
            return cxpr_fv_bool(false);
        }
        if (mode == CXPR_TIMESERIES_TREND_RISING) {
            if (!(lhs > rhs)) return cxpr_fv_bool(false);
        } else {
            if (!(lhs < rhs)) return cxpr_fv_bool(false);
        }
    }

    return cxpr_fv_bool(true);
}

/** @brief Native implementation for `rising(value, bars)`. */
static cxpr_value cxpr_timeseries_rising(const cxpr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_trend_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_TREND_RISING, err);
}

/** @brief Native implementation for `falling(value, bars)`. */
static cxpr_value cxpr_timeseries_falling(const cxpr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_trend_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_TREND_FALLING, err);
}

/**
 * @brief Register built-in native time-series predicates.
 * @param reg Destination registry.
 */
void cxpr_register_timeseries_builtins(cxpr_registry* reg) {
    if (!reg) return;

    cxpr_registry_add_timeseries(reg, "rising", cxpr_timeseries_rising, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "falling", cxpr_timeseries_falling, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
}
