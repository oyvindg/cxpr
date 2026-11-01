/**
 * @file builtin.c
 * @brief Single source of truth for cxpr builtin language metadata.
 */

#include <cxpr/builtin.h>

#include <string.h>

#define PARAMS(name, ...) static const char* const name[] = {__VA_ARGS__}
#define COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define INFO(name_, signature_, documentation_, params_, min_, max_, type_, flags_) \
    {name_, signature_, documentation_, params_, COUNT(params_), min_, max_, type_, flags_}

PARAMS(p_values, "value", "...values");
PARAMS(p_stacked_values, "first", "second", "...values");
PARAMS(p_value, "value");
PARAMS(p_left_right, "left", "right");
PARAMS(p_value_samples, "value", "samples");
PARAMS(p_left_right_bars, "left", "right", "bars");
PARAMS(p_value_samples_mode, "value", "samples", "mode");
PARAMS(p_expression_period, "expression", "period");
PARAMS(p_source_every, "source", "every");
PARAMS(p_three, "value", "minimum", "maximum");
PARAMS(p_start_end_value, "value", "start", "end", "inclusive_start", "inclusive_end");
PARAMS(p_condition_values, "condition", "true_value", "false_value");
PARAMS(p_x_y_t, "x", "y", "t");
PARAMS(p_edge0_edge1_x, "edge0", "edge1", "x");
PARAMS(p_x_midpoint_steepness, "x", "midpoint", "steepness");
PARAMS(p_basket_expression, "expression");

static const cxpr_function_info builtins[] = {
    INFO("min", "min(value, ...values)", "Returns the smallest value. Accepts 1 to 8 numeric arguments, or a trailing expression window such as min(window(expression, period)).", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT | CXPR_BUILTIN_BASKET),
    INFO("max", "max(value, ...values)", "Returns the largest value. Accepts 1 to 8 numeric arguments, or a trailing expression window such as max(window(expression, period)).", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT | CXPR_BUILTIN_BASKET),
    INFO("argmin", "argmin(value, ...values)", "Returns the zero-based index of the smallest value.", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT | CXPR_BUILTIN_BASKET),
    INFO("argmax", "argmax(value, ...values)", "Returns the zero-based index of the largest value.", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT | CXPR_BUILTIN_BASKET),
    INFO("sum", "sum(value, ...values)", "Returns the sum of the supplied values.", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT | CXPR_BUILTIN_BASKET),
    INFO("mean", "mean(value, ...values)", "Returns the arithmetic mean of the supplied values.", p_values, 1u, 8u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("stacked", "stacked(first, second, ...values)", "Returns true when every value is strictly lower than the preceding value.", p_stacked_values, 2u, 16u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
    INFO("clamp", "clamp(value, minimum, maximum)", "Constrains value to the inclusive minimum and maximum.", p_three, 3u, 3u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("sign", "sign(value)", "Returns -1, 0, or 1 according to the sign of value.", p_value, 1u, 1u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("add", "add(left, right)", "Adds two values.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("sub", "sub(left, right)", "Subtracts right from left.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("mul", "mul(left, right)", "Multiplies two values.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("div", "div(left, right)", "Divides left by right.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("lerp", "lerp(x, y, t)", "Linearly interpolates from x to y by t.", p_x_y_t, 3u, 3u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("smoothstep", "smoothstep(edge0, edge1, x)", "Returns smooth Hermite interpolation between two edges.", p_edge0_edge1_x, 3u, 3u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("sigmoid", "sigmoid(x, midpoint, steepness)", "Returns a configurable logistic sigmoid value.", p_x_midpoint_steepness, 3u, 3u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
#define UNARY(name_, doc_) INFO(name_, name_ "(value)", doc_, p_value, 1u, 1u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT)
    UNARY("abs", "Returns the absolute value."),
    UNARY("floor", "Rounds down to the nearest integer."),
    UNARY("ceil", "Rounds up to the nearest integer."),
    UNARY("round", "Rounds to the nearest integer."),
    UNARY("trunc", "Truncates the fractional part."),
    UNARY("sqrt", "Returns the square root."),
    UNARY("cbrt", "Returns the cube root."),
    INFO("hypot", "hypot(left, right)", "Returns the Euclidean length of two components.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("pow", "pow(left, right)", "Raises left to the power right.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    UNARY("exp", "Returns e raised to value."),
    UNARY("exp2", "Returns 2 raised to value."),
    UNARY("expm1", "Returns exp(value) minus one."),
    UNARY("log", "Returns the natural logarithm."),
    UNARY("log10", "Returns the base-10 logarithm."),
    UNARY("log2", "Returns the base-2 logarithm."),
    UNARY("log1p", "Returns log(1 + value) accurately for small values."),
    INFO("mod", "mod(left, right)", "Returns the floating-point remainder.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("copysign", "copysign(left, right)", "Returns left with the sign of right.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    UNARY("radians", "Converts degrees to radians."),
    UNARY("degrees", "Converts radians to degrees."),
    UNARY("sin", "Returns the sine in radians."),
    UNARY("cos", "Returns the cosine in radians."),
    UNARY("tan", "Returns the tangent in radians."),
    UNARY("asin", "Returns the arc sine in radians."),
    UNARY("acos", "Returns the arc cosine in radians."),
    UNARY("atan", "Returns the arc tangent in radians."),
    INFO("atan2", "atan2(left, right)", "Returns the quadrant-aware arc tangent.", p_left_right, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    UNARY("sinh", "Returns the hyperbolic sine."),
    UNARY("cosh", "Returns the hyperbolic cosine."),
    UNARY("tanh", "Returns the hyperbolic tangent."),
#undef UNARY
    {"pi", "pi()", "Returns the mathematical constant pi.", NULL, 0u, 0u, 0u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT},
    {"e", "e()", "Returns Euler's number.", NULL, 0u, 0u, 0u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT},
    {"nan", "nan()", "Returns a not-a-number value.", NULL, 0u, 0u, 0u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT},
    {"inf", "inf()", "Returns positive infinity.", NULL, 0u, 0u, 0u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT},
    INFO("if", "if(condition, true_value, false_value)", "Returns one of two values according to condition.", p_condition_values, 3u, 3u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_DEFAULT),
    INFO("contains", "contains(value, expression)", "Returns true when value contains expression.", p_left_right, 2u, 2u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
    INFO("within", "within(value, start, end, inclusive_start, inclusive_end)", "Returns true when value lies within the requested bounds.", p_start_end_value, 3u, 5u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
    INFO("isnan", "isnan(value)", "Returns true when value is not a number.", p_value, 1u, 1u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
    INFO("isfinite", "isfinite(value)", "Returns true when value is finite.", p_value, 1u, 1u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
    INFO("coalesce", "coalesce(value, ...values)", "Returns the first non-null value.", p_values, 1u, 8u, CXPR_VALUE_NULL, CXPR_BUILTIN_DEFAULT),
    INFO("is_null", "is_null(value)", "Returns true when value is null.", p_value, 1u, 1u, CXPR_VALUE_BOOL, CXPR_BUILTIN_DEFAULT),
#define SERIES(name_, signature_, doc_, params_, min_, max_, type_) \
    INFO(name_, signature_, doc_, params_, min_, max_, type_, CXPR_BUILTIN_TIMESERIES)
    SERIES("rising", "rising(value, samples)", "Returns true when value rises throughout the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("falling", "falling(value, samples)", "Returns true when value falls throughout the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("net_up", "net_up(value, samples)", "Returns true when value has a net increase over the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("net_down", "net_down(value, samples)", "Returns true when value has a net decrease over the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("repeat", "repeat(value, samples)", "Returns true when value repeats throughout the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("overlaps", "overlaps(left, right, bars)", "Returns true when two sequences overlap within the optional history depth.", p_left_right_bars, 2u, 3u, CXPR_VALUE_BOOL),
    SERIES("signal_overlaps", "signal_overlaps(left, right, bars)", "Returns true when two boolean sequences overlap within the optional history depth.", p_left_right_bars, 2u, 3u, CXPR_VALUE_BOOL),
    SERIES("cross_above", "cross_above(left, right)", "Returns true when left crosses from below to above right.", p_left_right, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("cross_below", "cross_below(left, right)", "Returns true when left crosses from above to below right.", p_left_right, 2u, 2u, CXPR_VALUE_BOOL),
    SERIES("delta", "delta(value, samples)", "Returns the change in value across the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_NUMBER),
    SERIES("roc", "roc(value, samples)", "Returns the rate of change across the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_NUMBER),
    SERIES("highest", "highest(value, samples)", "Returns the highest value in the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_NUMBER),
    SERIES("lowest", "lowest(value, samples)", "Returns the lowest value in the sample window.", p_value_samples, 2u, 2u, CXPR_VALUE_NUMBER),
    SERIES("bars_since_extreme", "bars_since_extreme(value, samples, mode)", "Returns the number of samples since the selected maximum or minimum.", p_value_samples_mode, 3u, 3u, CXPR_VALUE_NUMBER),
#undef SERIES
    INFO("avg", "avg(expression)", "Returns the basket average for expression.", p_basket_expression, 1u, 1u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_BASKET),
    INFO("any", "any(expression)", "Returns true when expression is true for any basket member.", p_basket_expression, 1u, 1u, CXPR_VALUE_BOOL, CXPR_BUILTIN_BASKET),
    INFO("all", "all(expression)", "Returns true when expression is true for every basket member.", p_basket_expression, 1u, 1u, CXPR_VALUE_BOOL, CXPR_BUILTIN_BASKET),
    INFO("count", "count(expression)", "Counts matching basket members.", p_basket_expression, 1u, 1u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_BASKET),
    INFO("window", "window(expression, period)", "Creates a compile-time trailing expression window for reducers such as mean and stddev; no runtime array is created.", p_expression_period, 2u, 2u, CXPR_VALUE_ARRAY, CXPR_BUILTIN_SYNTAX),
    INFO("stddev", "stddev(window(expression, period))", "Computes the standard deviation over a trailing expression window.", p_expression_period, 1u, 1u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_SYNTAX),
    INFO("resample", "resample(source, every)", "Selects a provider-backed series materialized at the requested fixed interval.", p_source_every, 2u, 2u, CXPR_VALUE_NUMBER, CXPR_BUILTIN_SYNTAX)
};

size_t cxpr_builtin_count(void) {
    return COUNT(builtins);
}

const cxpr_function_info* cxpr_builtin_at(size_t index) {
    return index < cxpr_builtin_count() ? &builtins[index] : NULL;
}

const cxpr_function_info* cxpr_builtin_find(const char* name) {
    if (!name) return NULL;
    for (size_t i = 0u; i < cxpr_builtin_count(); ++i) {
        if (strcmp(builtins[i].name, name) == 0) return &builtins[i];
    }
    return NULL;
}

#undef INFO
#undef COUNT
#undef PARAMS
