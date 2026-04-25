/**
 * @file defaults.c
 * @brief Builtin numeric helpers and default registration.
 */

#include "internal.h"

#include <math.h>

static double cxpr_clamp(double x, double lo, double hi) {
    if (lo > hi) {
        const double t = lo;
        lo = hi;
        hi = t;
    }
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static double cxpr_sign(double x) {
    return (x > 0.0) ? 1.0 : ((x < 0.0) ? -1.0 : 0.0);
}

static double cxpr_min_n(const double* args, size_t argc) {
    if (!args || argc == 0) return 0.0;
    double out = args[0];
    for (size_t i = 1; i < argc; ++i) {
        if (args[i] < out) out = args[i];
    }
    return out;
}

static double cxpr_max_n(const double* args, size_t argc) {
    if (!args || argc == 0) return 0.0;
    double out = args[0];
    for (size_t i = 1; i < argc; ++i) {
        if (args[i] > out) out = args[i];
    }
    return out;
}

static double cxpr_lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

static double cxpr_smoothstep(double x, double e0, double e1) {
    if (e0 == e1) return (x >= e1) ? 1.0 : 0.0;
    const double t = cxpr_clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

static double cxpr_sigmoid(double x, double center, double steepness) {
    return 1.0 / (1.0 + exp(-steepness * (x - center)));
}

static double cxpr_pi(void) {
    return 3.14159265358979323846;
}

static double cxpr_e(void) {
    return 2.71828182845904523536;
}

static double cxpr_nan(void) {
    return NAN;
}

static double cxpr_inf(void) {
    return INFINITY;
}

static double cxpr_if(double cond, double a, double b) {
    return (cond != 0.0) ? a : b;
}

double cxpr_unary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_unary_userdata* ud = (const cxpr_unary_userdata*)userdata;
    return ud->fn(args[0]);
}

double cxpr_binary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_binary_userdata* ud = (const cxpr_binary_userdata*)userdata;
    return ud->fn(args[0], args[1]);
}

double cxpr_nullary_adapter(const double* args, size_t argc, void* userdata) {
    (void)args;
    (void)argc;
    const cxpr_nullary_userdata* ud = (const cxpr_nullary_userdata*)userdata;
    return ud->fn();
}

double cxpr_ternary_adapter(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const cxpr_ternary_userdata* ud = (const cxpr_ternary_userdata*)userdata;
    return ud->fn(args[0], args[1], args[2]);
}

double cxpr_min(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    return cxpr_min_n(args, argc);
}

double cxpr_max(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    return cxpr_max_n(args, argc);
}

void cxpr_register_defaults(cxpr_registry* reg) {
    if (!reg) return;

    cxpr_registry_add(reg, "min", cxpr_min, 1, 8, NULL, NULL);
    cxpr_registry_add(reg, "max", cxpr_max, 1, 8, NULL, NULL);
    cxpr_registry_add_ternary(reg, "clamp", cxpr_clamp);
    cxpr_registry_add_unary(reg, "sign", cxpr_sign);
    cxpr_registry_add_ternary(reg, "lerp", cxpr_lerp);
    cxpr_registry_add_ternary(reg, "smoothstep", cxpr_smoothstep);
    cxpr_registry_add_ternary(reg, "sigmoid", cxpr_sigmoid);

    cxpr_registry_add_unary(reg, "abs", fabs);
    cxpr_registry_add_unary(reg, "floor", floor);
    cxpr_registry_add_unary(reg, "ceil", ceil);
    cxpr_registry_add_unary(reg, "round", round);
    cxpr_registry_add_unary(reg, "trunc", trunc);

    cxpr_registry_add_unary(reg, "sqrt", sqrt);
    cxpr_registry_add_unary(reg, "cbrt", cbrt);
    cxpr_registry_add_binary(reg, "pow", pow);
    cxpr_registry_add_unary(reg, "exp", exp);
    cxpr_registry_add_unary(reg, "exp2", exp2);

    cxpr_registry_add_unary(reg, "log", log);
    cxpr_registry_add_unary(reg, "log10", log10);
    cxpr_registry_add_unary(reg, "log2", log2);

    cxpr_registry_add_unary(reg, "sin", sin);
    cxpr_registry_add_unary(reg, "cos", cos);
    cxpr_registry_add_unary(reg, "tan", tan);
    cxpr_registry_add_unary(reg, "asin", asin);
    cxpr_registry_add_unary(reg, "acos", acos);
    cxpr_registry_add_unary(reg, "atan", atan);
    cxpr_registry_add_binary(reg, "atan2", atan2);

    cxpr_registry_add_unary(reg, "sinh", sinh);
    cxpr_registry_add_unary(reg, "cosh", cosh);
    cxpr_registry_add_unary(reg, "tanh", tanh);

    cxpr_registry_add_nullary(reg, "pi", cxpr_pi);
    cxpr_registry_add_nullary(reg, "e", cxpr_e);
    cxpr_registry_add_nullary(reg, "nan", cxpr_nan);
    cxpr_registry_add_nullary(reg, "inf", cxpr_inf);

    cxpr_registry_add_ternary(reg, "if", cxpr_if);
    cxpr_register_timeseries_builtins(reg);
}

void cxpr_register_builtins(cxpr_registry* reg) {
    cxpr_register_defaults(reg);
}
