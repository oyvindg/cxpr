/**
 * @file timeseries.c
 * @brief Native time-series helpers and builtins for cxpr.
 */

#include "registry/internal.h"
#include <cxpr/ast.h>
#include <math.h>

typedef enum {
    CXPR_TIMESERIES_TREND_RISING = 1,
    CXPR_TIMESERIES_TREND_FALLING = -1
} cxpr_timeseries_trend_mode;

/**
 * @brief Shared evaluator for monotonic trend predicates over historical offsets.
 * @param call_ast Function-call AST containing `(value_expr, lookback)`.
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
    const cxpr_ast* lookback_ast;
    double lookback_value = 0.0;
    long long lookback_ll;

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
                ? "rising(...) expects value, lookback"
                : "falling(...) expects value, lookback";
        }
        return cxpr_fv_bool(false);
    }

    value_ast = cxpr_ast_function_arg(call_ast, 0);
    lookback_ast = cxpr_ast_function_arg(call_ast, 1);

    if (!cxpr_eval_ast_number(lookback_ast, ctx, reg, &lookback_value, err)) {
        return cxpr_fv_bool(false);
    }

    lookback_ll = (long long)llround(lookback_value);
    if (!isfinite(lookback_value) || fabs(lookback_value - (double)lookback_ll) > 1e-9 || lookback_ll < 2) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = mode == CXPR_TIMESERIES_TREND_RISING
                ? "rising(...) requires integer lookback >= 2"
                : "falling(...) requires integer lookback >= 2";
        }
        return cxpr_fv_bool(false);
    }

    for (long long i = 0; i < lookback_ll - 1; ++i) {
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

/** @brief Native implementation for `rising(value, lookback)`. */
static cxpr_value cxpr_timeseries_rising(const cxpr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_trend_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_TREND_RISING, err);
}

/** @brief Native implementation for `falling(value, lookback)`. */
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
