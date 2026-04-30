/**
 * @file timeseries.c
 * @brief Native time-series helpers and builtins for cxpr.
 */

#include "registry/internal.h"
#include <cxpr/ast.h>
#include <math.h>
#include <string.h>

typedef enum {
    CXPR_TIMESERIES_TREND_RISING = 1,
    CXPR_TIMESERIES_TREND_FALLING = -1
} cxpr_timeseries_trend_mode;

typedef enum {
    CXPR_TIMESERIES_CROSS_ABOVE = 1,
    CXPR_TIMESERIES_CROSS_BELOW = -1
} cxpr_timeseries_cross_mode;

typedef enum {
    CXPR_TIMESERIES_WINDOW_HIGHEST = 1,
    CXPR_TIMESERIES_WINDOW_LOWEST = -1
} cxpr_timeseries_window_mode;

static cxpr_value cxpr_timeseries_call_error(
    const cxpr_ast* call_ast,
    cxpr_error* err) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = "Time-series function expects a call AST";
    }
    (void)call_ast;
    return cxpr_fv_double(NAN);
}

static int cxpr_timeseries_read_bars(
    const cxpr_ast* call_ast,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    long long min_bars,
    long long* out_bars,
    cxpr_error* err) {
    const cxpr_ast* bars_ast;
    double bars_value = 0.0;
    long long bars_ll;

    if (!call_ast || cxpr_ast_type(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return 0;
    }
    if (cxpr_ast_function_argc(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Time-series function expects value, bars";
        }
        return 0;
    }

    bars_ast = cxpr_ast_function_arg(call_ast, 1);
    if (!cxpr_eval_ast_number(bars_ast, ctx, reg, &bars_value, err)) return 0;

    bars_ll = (long long)llround(bars_value);
    if (!isfinite(bars_value) || fabs(bars_value - (double)bars_ll) > 1e-9 ||
        bars_ll < min_bars) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series bars must be an integer above the minimum";
        }
        return 0;
    }

    if (out_bars) *out_bars = bars_ll;
    return 1;
}

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
                ? "rising(...) expects value, lookback"
                : "falling(...) expects value, lookback";
        }
        return cxpr_fv_bool(false);
    }

    value_ast = cxpr_ast_function_arg(call_ast, 0);
    if (!cxpr_timeseries_read_bars(call_ast, ctx, reg, 2, &bars_ll, err)) {
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

static cxpr_value cxpr_timeseries_cross_eval(const cxpr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             cxpr_timeseries_cross_mode mode,
                                             cxpr_error* err) {
    const cxpr_ast* left_ast;
    const cxpr_ast* right_ast;
    double left = 0.0;
    double right = 0.0;
    double prev_left = 0.0;
    double prev_right = 0.0;

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
            err->message = mode == CXPR_TIMESERIES_CROSS_ABOVE
                ? "cross_above(...) expects left, right"
                : "cross_below(...) expects left, right";
        }
        return cxpr_fv_bool(false);
    }

    left_ast = cxpr_ast_function_arg(call_ast, 0);
    right_ast = cxpr_ast_function_arg(call_ast, 1);
    if (!cxpr_eval_ast_number_at_offset(left_ast, 0.0, ctx, reg, &left, err) ||
        !cxpr_eval_ast_number_at_offset(right_ast, 0.0, ctx, reg, &right, err) ||
        !cxpr_eval_ast_number_at_offset(left_ast, 1.0, ctx, reg, &prev_left, err) ||
        !cxpr_eval_ast_number_at_offset(right_ast, 1.0, ctx, reg, &prev_right, err)) {
        return cxpr_fv_bool(false);
    }

    if (mode == CXPR_TIMESERIES_CROSS_ABOVE) {
        return cxpr_fv_bool(prev_left <= prev_right && left > right);
    }
    return cxpr_fv_bool(prev_left >= prev_right && left < right);
}

static cxpr_value cxpr_timeseries_delta(const cxpr_ast* call_ast,
                                        const cxpr_context* ctx,
                                        const cxpr_registry* reg,
                                        void* userdata,
                                        cxpr_error* err) {
    const cxpr_ast* value_ast;
    long long bars_ll;
    double value = 0.0;
    double previous = 0.0;

    (void)userdata;
    if (!call_ast || cxpr_ast_type(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_ast_function_arg(call_ast, 0);
    if (!cxpr_timeseries_read_bars(call_ast, ctx, reg, 1, &bars_ll, err)) {
        return cxpr_fv_double(NAN);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)bars_ll, ctx, reg, &previous, err)) {
        return cxpr_fv_double(NAN);
    }
    return cxpr_fv_double(value - previous);
}

static cxpr_value cxpr_timeseries_roc(const cxpr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      void* userdata,
                                      cxpr_error* err) {
    const cxpr_ast* value_ast;
    long long bars_ll;
    double value = 0.0;
    double previous = 0.0;

    (void)userdata;
    if (!call_ast || cxpr_ast_type(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_ast_function_arg(call_ast, 0);
    if (!cxpr_timeseries_read_bars(call_ast, ctx, reg, 1, &bars_ll, err)) {
        return cxpr_fv_double(NAN);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)bars_ll, ctx, reg, &previous, err)) {
        return cxpr_fv_double(NAN);
    }
    if (previous == 0.0) return cxpr_fv_double(NAN);
    return cxpr_fv_double((value - previous) / previous);
}

static cxpr_value cxpr_timeseries_window_eval(const cxpr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              cxpr_timeseries_window_mode mode,
                                              cxpr_error* err) {
    const cxpr_ast* value_ast;
    long long bars_ll;
    double out = 0.0;

    if (!call_ast || cxpr_ast_type(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_ast_function_arg(call_ast, 0);
    if (!cxpr_timeseries_read_bars(call_ast, ctx, reg, 1, &bars_ll, err)) {
        return cxpr_fv_double(NAN);
    }

    for (long long i = 0; i < bars_ll; ++i) {
        double value = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_fv_double(NAN);
        }
        if (i == 0 ||
            (mode == CXPR_TIMESERIES_WINDOW_HIGHEST && value > out) ||
            (mode == CXPR_TIMESERIES_WINDOW_LOWEST && value < out)) {
            out = value;
        }
    }
    return cxpr_fv_double(out);
}

static cxpr_value cxpr_timeseries_cross_above(const cxpr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              void* userdata,
                                              cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_cross_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_CROSS_ABOVE, err);
}

static cxpr_value cxpr_timeseries_cross_below(const cxpr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              void* userdata,
                                              cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_cross_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_CROSS_BELOW, err);
}

static cxpr_value cxpr_timeseries_highest(const cxpr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_WINDOW_HIGHEST, err);
}

static cxpr_value cxpr_timeseries_lowest(const cxpr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_WINDOW_LOWEST, err);
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
    cxpr_registry_add_timeseries(reg, "cross_above", cxpr_timeseries_cross_above, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "cross_below", cxpr_timeseries_cross_below, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "delta", cxpr_timeseries_delta, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "roc", cxpr_timeseries_roc, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "highest", cxpr_timeseries_highest, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "lowest", cxpr_timeseries_lowest, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
}

bool cxpr_timeseries_is_builtin(const char* name) {
    if (!name) return false;
    return strcmp(name, "rising") == 0 ||
           strcmp(name, "falling") == 0 ||
           strcmp(name, "cross_above") == 0 ||
           strcmp(name, "cross_below") == 0 ||
           strcmp(name, "delta") == 0 ||
           strcmp(name, "roc") == 0 ||
           strcmp(name, "highest") == 0 ||
           strcmp(name, "lowest") == 0;
}
