/**
 * @file timeseries.c
 * @brief Native time-series helpers and builtins for cxpr.
 */

#include "registry/internal.h" // IWYU pragma: keep
#include <cxpr/expr/ast.h>
#include <cxpr/window.h>
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

typedef enum {
    CXPR_TIMESERIES_AGG_SUM,
    CXPR_TIMESERIES_AGG_MEAN,
    CXPR_TIMESERIES_AGG_HIGHEST,
    CXPR_TIMESERIES_AGG_LOWEST,
    CXPR_TIMESERIES_AGG_STDDEV
} cxpr_timeseries_agg_mode;

typedef enum {
    CXPR_TIMESERIES_NET_UP = 1,
    CXPR_TIMESERIES_NET_DOWN = -1
} cxpr_timeseries_net_mode;

static cxpr_value cxpr_timeseries_call_error(
    const cxpr_expr_ast* call_ast,
    cxpr_error* err) {
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = "Time-series function expects a call AST";
    }
    (void)call_ast;
    return cxpr_num(NAN);
}

static int cxpr_timeseries_read_samples(
    const cxpr_expr_ast* call_ast,
    const cxpr_context* ctx,
    const cxpr_registry* reg,
    long long min_samples,
    long long* out_samples,
    cxpr_error* err) {
    const cxpr_expr_ast* samples_ast;
    double samples_value = 0.0;
    long long samples_ll;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return 0;
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "Time-series function expects value, samples";
        }
        return 0;
    }

    samples_ast = cxpr_expr_ast_call_arg(call_ast, 1);
    if (!cxpr_eval_ast_number(samples_ast, ctx, reg, &samples_value, err)) return 0;

    samples_ll = (long long)llround(samples_value);
    if (!isfinite(samples_value) || fabs(samples_value - (double)samples_ll) > 1e-9 ||
        samples_ll < min_samples) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series samples must be an integer above the minimum";
        }
        return 0;
    }

    if (out_samples) *out_samples = samples_ll;
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
static cxpr_value cxpr_timeseries_trend_eval(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             cxpr_timeseries_trend_mode mode,
                                             cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return cxpr_bool(false);
    }

    if (cxpr_expr_ast_call_arg_count(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = mode == CXPR_TIMESERIES_TREND_RISING
                ? "rising(...) expects value, samples"
                : "falling(...) expects value, samples";
        }
        return cxpr_bool(false);
    }

    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 2, &samples_ll, err)) {
        return cxpr_bool(false);
    }

    for (long long i = 0; i < samples_ll - 1; ++i) {
        double lhs = 0.0;
        double rhs = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &lhs, err) ||
            !cxpr_eval_ast_number_at_offset(value_ast, (double)(i + 1), ctx, reg, &rhs, err)) {
            return cxpr_bool(false);
        }
        if (mode == CXPR_TIMESERIES_TREND_RISING) {
            if (!(lhs > rhs)) return cxpr_bool(false);
        } else {
            if (!(lhs < rhs)) return cxpr_bool(false);
        }
    }

    return cxpr_bool(true);
}

static cxpr_value cxpr_timeseries_cross_eval(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             cxpr_timeseries_cross_mode mode,
                                             cxpr_error* err) {
    const cxpr_expr_ast* left_ast;
    const cxpr_expr_ast* right_ast;
    double left = 0.0;
    double right = 0.0;
    double prev_left = 0.0;
    double prev_right = 0.0;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return cxpr_bool(false);
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = mode == CXPR_TIMESERIES_CROSS_ABOVE
                ? "cross_above(...) expects left, right"
                : "cross_below(...) expects left, right";
        }
        return cxpr_bool(false);
    }

    left_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    right_ast = cxpr_expr_ast_call_arg(call_ast, 1);
    if (!cxpr_eval_ast_number_at_offset(left_ast, 0.0, ctx, reg, &left, err) ||
        !cxpr_eval_ast_number_at_offset(right_ast, 0.0, ctx, reg, &right, err) ||
        !cxpr_eval_ast_number_at_offset(left_ast, 1.0, ctx, reg, &prev_left, err) ||
        !cxpr_eval_ast_number_at_offset(right_ast, 1.0, ctx, reg, &prev_right, err)) {
        return cxpr_bool(false);
    }

    if (mode == CXPR_TIMESERIES_CROSS_ABOVE) {
        return cxpr_bool(prev_left <= prev_right && left > right);
    }
    return cxpr_bool(prev_left >= prev_right && left < right);
}

static cxpr_value cxpr_timeseries_delta(const cxpr_expr_ast* call_ast,
                                        const cxpr_context* ctx,
                                        const cxpr_registry* reg,
                                        void* userdata,
                                        cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double value = 0.0;
    double previous = 0.0;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)samples_ll, ctx, reg, &previous, err)) {
        return cxpr_num(NAN);
    }
    return cxpr_num(value - previous);
}

static cxpr_value cxpr_timeseries_roc(const cxpr_expr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      void* userdata,
                                      cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double value = 0.0;
    double previous = 0.0;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)samples_ll, ctx, reg, &previous, err)) {
        return cxpr_num(NAN);
    }
    if (previous == 0.0) return cxpr_num(NAN);
    return cxpr_num((value - previous) / previous);
}

static cxpr_value cxpr_timeseries_window_eval(const cxpr_expr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              cxpr_timeseries_window_mode mode,
                                              cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double out = 0.0;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }

    for (long long i = 0; i < samples_ll; ++i) {
        double value = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_num(NAN);
        }
        if (i == 0 ||
            (mode == CXPR_TIMESERIES_WINDOW_HIGHEST && value > out) ||
            (mode == CXPR_TIMESERIES_WINDOW_LOWEST && value < out)) {
            out = value;
        }
    }
    return cxpr_num(out);
}

static cxpr_value cxpr_timeseries_window_agg_eval(const cxpr_expr_ast* call_ast,
                                                  const cxpr_context* ctx,
                                                  const cxpr_registry* reg,
                                                  cxpr_timeseries_agg_mode mode,
                                                  cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double sum = 0.0;
    double sumsq = 0.0;
    double extreme = 0.0;
    long long count = 0;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }
    for (long long i = 0; i < samples_ll; ++i) {
        double value = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_num(NAN);
        }
        if (isnan(value)) continue;
        if (count == 0 ||
            (mode == CXPR_TIMESERIES_AGG_HIGHEST && value > extreme) ||
            (mode == CXPR_TIMESERIES_AGG_LOWEST && value < extreme)) {
            extreme = value;
        }
        sum += value;
        sumsq += value * value;
        count++;
    }
    if (count == 0) return cxpr_num(0.0);
    if (mode == CXPR_TIMESERIES_AGG_HIGHEST ||
        mode == CXPR_TIMESERIES_AGG_LOWEST) {
        return cxpr_num(extreme);
    }
    if (mode == CXPR_TIMESERIES_AGG_MEAN) {
        return cxpr_num(sum / (double)count);
    }
    if (mode == CXPR_TIMESERIES_AGG_STDDEV) {
        double mean = sum / (double)count;
        double variance = (sumsq / (double)count) - mean * mean;
        return cxpr_num(sqrt(variance > 0.0 ? variance : 0.0));
    }
    return cxpr_num(sum);
}

static cxpr_value cxpr_timeseries_bars_since_extreme(const cxpr_expr_ast* call_ast,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     void* userdata,
                                                     cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* samples_ast;
    const cxpr_expr_ast* mode_ast;
    double samples_value = 0.0;
    double mode = 0.0;
    long long samples_ll;
    double extreme = 0.0;
    long long extreme_index = 0;
    long long count = 0;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 3u) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "bars_since_extreme expects value, samples, mode";
        }
        return cxpr_num(NAN);
    }

    value_ast = cxpr_expr_ast_call_arg(call_ast, 0u);
    samples_ast = cxpr_expr_ast_call_arg(call_ast, 1u);
    mode_ast = cxpr_expr_ast_call_arg(call_ast, 2u);
    if (!cxpr_eval_ast_number(samples_ast, ctx, reg, &samples_value, err) ||
        !cxpr_eval_ast_number(mode_ast, ctx, reg, &mode, err)) {
        return cxpr_num(NAN);
    }
    samples_ll = (long long)llround(samples_value);
    if (!isfinite(samples_value) || fabs(samples_value - (double)samples_ll) > 1e-9 ||
        samples_ll < 1) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "bars_since_extreme samples must be a positive integer";
        }
        return cxpr_num(NAN);
    }

    for (long long i = 0; i < samples_ll; ++i) {
        double value = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_num(NAN);
        }
        if (isnan(value)) continue;
        if (count == 0 ||
            (mode >= 0.0 && value > extreme) ||
            (mode < 0.0 && value < extreme)) {
            extreme = value;
            extreme_index = i;
        }
        count++;
    }
    return cxpr_num(count == 0 ? 0.0 : (double)extreme_index);
}

static cxpr_value cxpr_timeseries_window_mean_absdev(const cxpr_expr_ast* call_ast,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     void* userdata,
                                                     cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    const cxpr_expr_ast* samples_ast;
    const cxpr_expr_ast* center_ast;
    double samples_value = 0.0;
    double center = 0.0;
    long long samples_ll;
    double sum = 0.0;
    long long count = 0;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 3u) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "window_mean_absdev expects value, samples, center";
        }
        return cxpr_num(NAN);
    }

    value_ast = cxpr_expr_ast_call_arg(call_ast, 0u);
    samples_ast = cxpr_expr_ast_call_arg(call_ast, 1u);
    center_ast = cxpr_expr_ast_call_arg(call_ast, 2u);
    if (!cxpr_eval_ast_number(samples_ast, ctx, reg, &samples_value, err) ||
        !cxpr_eval_ast_number(center_ast, ctx, reg, &center, err)) {
        return cxpr_num(NAN);
    }
    samples_ll = (long long)llround(samples_value);
    if (!isfinite(samples_value) || fabs(samples_value - (double)samples_ll) > 1e-9 ||
        samples_ll < 1) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "window_mean_absdev samples must be a positive integer";
        }
        return cxpr_num(NAN);
    }
    if (isnan(center)) return cxpr_num(0.0);

    for (long long i = 0; i < samples_ll; ++i) {
        double value = 0.0;
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_num(NAN);
        }
        if (isnan(value)) continue;
        sum += fabs(value - center);
        count++;
    }
    return cxpr_num(count == 0 ? 0.0 : sum / (double)count);
}

static cxpr_value cxpr_timeseries_window_sum(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             void* userdata,
                                             cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_agg_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_AGG_SUM, err);
}

static cxpr_value cxpr_timeseries_window_mean(const cxpr_expr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              void* userdata,
                                              cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_agg_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_AGG_MEAN, err);
}

static cxpr_value cxpr_timeseries_window_wma(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             void* userdata,
                                             cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double weighted_sum = 0.0;
    double weight_sum = 0.0;
    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }
    for (long long i = 0; i < samples_ll; ++i) {
        double value = 0.0;
        double weight = (double)(samples_ll - i);
        if (!cxpr_eval_ast_number_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_num(NAN);
        }
        if (isnan(value)) continue;
        weighted_sum += value * weight;
        weight_sum += weight;
    }
    return cxpr_num(weight_sum > 0.0 ? weighted_sum / weight_sum : 0.0);
}

static cxpr_value cxpr_timeseries_window_highest_value(const cxpr_expr_ast* call_ast,
                                                       const cxpr_context* ctx,
                                                       const cxpr_registry* reg,
                                                       void* userdata,
                                                       cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_agg_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_AGG_HIGHEST, err);
}

static cxpr_value cxpr_timeseries_window_lowest_value(const cxpr_expr_ast* call_ast,
                                                      const cxpr_context* ctx,
                                                      const cxpr_registry* reg,
                                                      void* userdata,
                                                      cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_agg_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_AGG_LOWEST, err);
}

static cxpr_value cxpr_timeseries_window_stddev(const cxpr_expr_ast* call_ast,
                                                const cxpr_context* ctx,
                                                const cxpr_registry* reg,
                                                void* userdata,
                                                cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_agg_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_AGG_STDDEV, err);
}

static cxpr_value cxpr_timeseries_window_roc(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             void* userdata,
                                             cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double value = 0.0;
    double previous = 0.0;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_num(NAN);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)samples_ll, ctx, reg, &previous, err)) {
        return cxpr_num(NAN);
    }
    if (isnan(value)) return cxpr_num(NAN);
    if (isnan(previous) || fabs(previous) <= 1e-12) return cxpr_num(0.0);
    return cxpr_num(((value - previous) / previous) * 100.0);
}

static cxpr_value cxpr_timeseries_cross_above(const cxpr_expr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              void* userdata,
                                              cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_cross_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_CROSS_ABOVE, err);
}

static cxpr_value cxpr_timeseries_cross_below(const cxpr_expr_ast* call_ast,
                                              const cxpr_context* ctx,
                                              const cxpr_registry* reg,
                                              void* userdata,
                                              cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_cross_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_CROSS_BELOW, err);
}

static cxpr_value cxpr_timeseries_highest(const cxpr_expr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_WINDOW_HIGHEST, err);
}

static cxpr_value cxpr_timeseries_lowest(const cxpr_expr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_window_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_WINDOW_LOWEST, err);
}

/** @brief Native implementation for `rising(value, samples)`. */
static cxpr_value cxpr_timeseries_rising(const cxpr_expr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_trend_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_TREND_RISING, err);
}

/** @brief Native implementation for `falling(value, samples)`. */
static cxpr_value cxpr_timeseries_falling(const cxpr_expr_ast* call_ast,
                                          const cxpr_context* ctx,
                                          const cxpr_registry* reg,
                                          void* userdata,
                                          cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_trend_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_TREND_FALLING, err);
}

static cxpr_value cxpr_timeseries_net_eval(const cxpr_expr_ast* call_ast,
                                           const cxpr_context* ctx,
                                           const cxpr_registry* reg,
                                           cxpr_timeseries_net_mode mode,
                                           cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;
    double value = 0.0;
    double previous = 0.0;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Time-series function expects a call AST";
        }
        return cxpr_bool(false);
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = mode == CXPR_TIMESERIES_NET_UP
                ? "net_up(...) expects value, samples"
                : "net_down(...) expects value, samples";
        }
        return cxpr_bool(false);
    }

    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_bool(false);
    }
    if (!cxpr_eval_ast_number_at_offset(value_ast, 0.0, ctx, reg, &value, err) ||
        !cxpr_eval_ast_number_at_offset(value_ast, (double)samples_ll, ctx, reg, &previous, err)) {
        return cxpr_bool(false);
    }
    if (!isfinite(value) || !isfinite(previous)) return cxpr_bool(false);
    if (mode == CXPR_TIMESERIES_NET_UP) {
        return cxpr_bool(value > previous);
    }
    return cxpr_bool(value < previous);
}

static cxpr_value cxpr_timeseries_net_up(const cxpr_expr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_net_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_NET_UP, err);
}

static cxpr_value cxpr_timeseries_net_down(const cxpr_expr_ast* call_ast,
                                           const cxpr_context* ctx,
                                           const cxpr_registry* reg,
                                           void* userdata,
                                           cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_net_eval(
        call_ast, ctx, reg, CXPR_TIMESERIES_NET_DOWN, err);
}

/** @brief Native implementation for `repeat(condition, samples)`. */
static cxpr_value cxpr_timeseries_repeat(const cxpr_expr_ast* call_ast,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_error* err) {
    const cxpr_expr_ast* value_ast;
    long long samples_ll;

    (void)userdata;
    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    if (cxpr_expr_ast_call_arg_count(call_ast) != 2) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "repeat(...) expects condition, samples";
        }
        return cxpr_bool(false);
    }
    value_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    if (!cxpr_timeseries_read_samples(call_ast, ctx, reg, 1, &samples_ll, err)) {
        return cxpr_bool(false);
    }
    for (long long i = 0; i < samples_ll; ++i) {
        bool value = false;
        if (!cxpr_eval_ast_bool_at_offset(value_ast, (double)i, ctx, reg, &value, err)) {
            return cxpr_bool(false);
        }
        if (!value) return cxpr_bool(false);
    }
    return cxpr_bool(true);
}

static int cxpr_timeseries_read_overlap_bars(const cxpr_expr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             long long* out_bars,
                                             cxpr_error* err) {
    const size_t argc = cxpr_expr_ast_call_arg_count(call_ast);
    const cxpr_expr_ast* bars_ast;
    double bars_value = 0.0;
    long long bars_ll;

    if (!out_bars) return 0;
    *out_bars = 0;
    if (argc == 2u) return 1;
    if (argc != 3u) {
        if (err) {
            err->code = CXPR_ERR_WRONG_ARITY;
            err->message = "overlaps(...) expects left, right[, bars]";
        }
        return 0;
    }

    bars_ast = cxpr_expr_ast_call_arg(call_ast, 2);
    if (!cxpr_eval_ast_number(bars_ast, ctx, reg, &bars_value, err)) return 0;

    bars_ll = (long long)llround(bars_value);
    if (!isfinite(bars_value) || fabs(bars_value - (double)bars_ll) > 1e-9 ||
        bars_ll < 0) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "overlaps(...) bars must be an integer >= 0";
        }
        return 0;
    }

    *out_bars = bars_ll;
    return 1;
}

static cxpr_value cxpr_timeseries_overlap_eval(const cxpr_expr_ast* call_ast,
                                               const cxpr_context* ctx,
                                               const cxpr_registry* reg,
                                               cxpr_error* err) {
    const cxpr_expr_ast* left_ast;
    const cxpr_expr_ast* right_ast;
    long long bars_ll;
    bool left_seen = false;
    bool right_seen = false;

    if (!call_ast || cxpr_expr_ast_kind_of(call_ast) != CXPR_NODE_FUNCTION_CALL) {
        return cxpr_timeseries_call_error(call_ast, err);
    }
    if (!cxpr_timeseries_read_overlap_bars(call_ast, ctx, reg, &bars_ll, err)) {
        return cxpr_bool(false);
    }

    left_ast = cxpr_expr_ast_call_arg(call_ast, 0);
    right_ast = cxpr_expr_ast_call_arg(call_ast, 1);
    for (long long i = 0; i <= bars_ll; ++i) {
        bool left_value = false;
        bool right_value = false;

        if (!left_seen &&
            !cxpr_eval_ast_bool_at_offset(left_ast, (double)i, ctx, reg, &left_value, err)) {
            return cxpr_bool(false);
        }
        if (!right_seen &&
            !cxpr_eval_ast_bool_at_offset(right_ast, (double)i, ctx, reg, &right_value, err)) {
            return cxpr_bool(false);
        }
        left_seen = left_seen || left_value;
        right_seen = right_seen || right_value;
        if (left_seen && right_seen) return cxpr_bool(true);
    }
    return cxpr_bool(false);
}

static cxpr_value cxpr_timeseries_overlaps(const cxpr_expr_ast* call_ast,
                                           const cxpr_context* ctx,
                                           const cxpr_registry* reg,
                                           void* userdata,
                                           cxpr_error* err) {
    (void)userdata;
    return cxpr_timeseries_overlap_eval(call_ast, ctx, reg, err);
}

/**
 * @brief Register built-in native time-series predicates.
 * @param reg Destination registry.
 */
void cxpr_register_timeseries(cxpr_registry* reg) {
    static const char* const value_samples_params[] = {"value", "samples"};
    static const char* const overlap_params[] = {"left", "right", "bars"};

    if (!reg) return;

    cxpr_registry_add_timeseries(reg, "rising", cxpr_timeseries_rising, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "rising", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "falling", cxpr_timeseries_falling, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "falling", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "net_up", cxpr_timeseries_net_up, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "net_up", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "net_down", cxpr_timeseries_net_down, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "net_down", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "repeat", cxpr_timeseries_repeat, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "repeat", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "overlaps", cxpr_timeseries_overlaps, 2, 3,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "overlaps", overlap_params, 3u);
    cxpr_registry_add_timeseries(reg, "signal_overlaps", cxpr_timeseries_overlaps, 2, 3,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_set_param_names(reg, "signal_overlaps", overlap_params, 3u);
    cxpr_registry_add_timeseries(reg, "cross_above", cxpr_timeseries_cross_above, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "cross_below", cxpr_timeseries_cross_below, 2, 2,
                                 CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_timeseries(reg, "delta", cxpr_timeseries_delta, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_set_param_names(reg, "delta", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "roc", cxpr_timeseries_roc, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_set_param_names(reg, "roc", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "highest", cxpr_timeseries_highest, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_set_param_names(reg, "highest", value_samples_params, 2u);
    cxpr_registry_add_timeseries(reg, "lowest", cxpr_timeseries_lowest, 2, 2,
                                 CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_registry_set_param_names(reg, "lowest", value_samples_params, 2u);
    for (size_t i = 0u; i < cxpr_window_ir_count(); ++i) {
        static const char* bars_since_extreme_params[] = {"value", "samples", "mode"};
        static const char* window_mean_absdev_params[] = {"value", "samples", "center"};
        const cxpr_window_ir* window = cxpr_window_ir_at(i);
        cxpr_timeseries_func_ptr fn = NULL;
        const char* const* params = value_samples_params;
        switch (window->op) {
        case CXPR_WINDOW_OP_SUM: fn = cxpr_timeseries_window_sum; break;
        case CXPR_WINDOW_OP_MEAN: fn = cxpr_timeseries_window_mean; break;
        case CXPR_WINDOW_OP_WMA: fn = cxpr_timeseries_window_wma; break;
        case CXPR_WINDOW_OP_HIGHEST:
            fn = cxpr_timeseries_window_highest_value;
            break;
        case CXPR_WINDOW_OP_LOWEST:
            fn = cxpr_timeseries_window_lowest_value;
            break;
        case CXPR_WINDOW_OP_STDDEV: fn = cxpr_timeseries_window_stddev; break;
        case CXPR_WINDOW_OP_ROC: fn = cxpr_timeseries_window_roc; break;
        case CXPR_WINDOW_OP_BARS_SINCE_EXTREME:
            fn = cxpr_timeseries_bars_since_extreme;
            params = bars_since_extreme_params;
            break;
        case CXPR_WINDOW_OP_MEAN_ABSDEV:
            fn = cxpr_timeseries_window_mean_absdev;
            params = window_mean_absdev_params;
            break;
        default: break;
        }
        if (!fn) continue;
        cxpr_registry_add_timeseries(
            reg, window->name, fn, (int)window->arity, (int)window->arity,
            CXPR_VALUE_NUMBER, NULL, NULL);
        cxpr_registry_set_param_names(reg, window->name, params, window->arity);
    }
}

bool cxpr_timeseries_is_builtin(const char* name) {
    if (!name) return false;
    return strcmp(name, "rising") == 0 ||
           strcmp(name, "falling") == 0 ||
           strcmp(name, "net_up") == 0 ||
           strcmp(name, "net_down") == 0 ||
           strcmp(name, "repeat") == 0 ||
           strcmp(name, "overlaps") == 0 ||
           strcmp(name, "signal_overlaps") == 0 ||
           strcmp(name, "cross_above") == 0 ||
           strcmp(name, "cross_below") == 0 ||
           strcmp(name, "delta") == 0 ||
           strcmp(name, "roc") == 0 ||
           strcmp(name, "highest") == 0 ||
           strcmp(name, "lowest") == 0 ||
           cxpr_window_ir_find(name) != NULL;
}
