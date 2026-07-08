/**
 * @file defaults.c
 * @brief Builtin numeric helpers and default registration.
 */

#include "internal.h"

#include <cxpr/ast.h>

#include <math.h>
#include <string.h>

/* Single source of truth for the math constants the builtins expose, so the
 * `pi`/`e` constant functions and the radians/degrees conversions never drift.
 * Spelled out as literals (not M_PI) to stay portable across C compilers. */
#define CXPR_PI 3.14159265358979323846
#define CXPR_E  2.71828182845904523536

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

static double cxpr_add(double a, double b) {
    return a + b;
}

static double cxpr_sub(double a, double b) {
    return a - b;
}

static double cxpr_mul(double a, double b) {
    return a * b;
}

static double cxpr_div(double a, double b) {
    return a / b;
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
    return CXPR_PI;
}

static double cxpr_e(void) {
    return CXPR_E;
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

static double cxpr_radians(double degrees) {
    return degrees * (CXPR_PI / 180.0);
}

static double cxpr_degrees(double radians) {
    return radians * (180.0 / CXPR_PI);
}

static cxpr_value cxpr_fn_isnan(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(isnan(args[0].d) != 0);
}

static cxpr_value cxpr_fn_isfinite(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(isfinite(args[0].d) != 0);
}

static cxpr_value cxpr_fn_coalesce(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    for (size_t i = 0; i < argc; ++i) {
        if (args[i].type != CXPR_VALUE_NULL) return args[i];
    }
    return cxpr_null();
}

static cxpr_value cxpr_fn_is_null(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(args[0].type == CXPR_VALUE_NULL);
}

static int cxpr_contains_scalar_equal(cxpr_value left, cxpr_value right, bool* out) {
    if (!out) return 0;
    *out = false;
    if (left.type != right.type) return 1;

    switch (left.type) {
    case CXPR_VALUE_NUMBER:
        *out = left.d == right.d;
        return 1;
    case CXPR_VALUE_BOOL:
        *out = left.b == right.b;
        return 1;
    case CXPR_VALUE_STRING:
        *out = strcmp(left.str, right.str) == 0;
        return 1;
    case CXPR_VALUE_NULL:
        *out = true;
        return 1;
    case CXPR_VALUE_TIMESTAMP:
    case CXPR_VALUE_DURATION:
        *out = left.i64 == right.i64;
        return 1;
    default:
        return 0;
    }
}

static cxpr_value cxpr_contains_error(cxpr_error* err,
                                      cxpr_error_code code,
                                      const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return cxpr_bool(false);
}

static int cxpr_contains_param_index(const char* name) {
    if (!name) return -1;
    if (strcmp(name, "source") == 0 || strcmp(name, "value") == 0) return 0;
    if (strcmp(name, "values") == 0 || strcmp(name, "array") == 0 ||
        strcmp(name, "set") == 0) {
        return 1;
    }
    return -1;
}

static cxpr_value cxpr_fn_contains(const cxpr_ast* call_ast,
                                   const cxpr_context* ctx,
                                   const cxpr_registry* reg,
                                   void* userdata,
                                   cxpr_error* err) {
    const size_t argc = cxpr_ast_function_argc(call_ast);
    const cxpr_ast* ordered[2] = {0};
    bool used[2] = {false, false};
    bool seen_named = false;
    size_t positional = 0u;
    cxpr_value source;
    cxpr_value values;

    (void)userdata;
    if (argc != 2u) {
        return cxpr_contains_error(err, CXPR_ERR_WRONG_ARITY,
                                   "contains() expects source and values");
    }

    for (size_t i = 0u; i < argc; ++i) {
        const cxpr_ast* arg = cxpr_ast_function_arg(call_ast, i);
        const char* arg_name = cxpr_ast_function_arg_name(call_ast, i);
        int index;

        if (!arg_name) {
            if (seen_named) {
                return cxpr_contains_error(err, CXPR_ERR_SYNTAX,
                                           "Positional arguments cannot follow named arguments");
            }
            ordered[positional] = arg;
            used[positional] = true;
            ++positional;
            continue;
        }

        seen_named = true;
        index = cxpr_contains_param_index(arg_name);
        if (index < 0) {
            return cxpr_contains_error(err, CXPR_ERR_SYNTAX,
                                       "Unknown named argument for contains()");
        }
        if (used[index]) {
            return cxpr_contains_error(err, CXPR_ERR_SYNTAX,
                                       "Duplicate argument for contains()");
        }
        ordered[index] = arg;
        used[index] = true;
    }

    if (!ordered[0] || !ordered[1]) {
        return cxpr_contains_error(err, CXPR_ERR_WRONG_ARITY,
                                   "contains() requires source and values");
    }
    if (!cxpr_eval_ast(ordered[0], ctx, reg, &source, err)) return cxpr_bool(false);
    if (!cxpr_eval_ast(ordered[1], ctx, reg, &values, err)) return cxpr_bool(false);
    if (values.type != CXPR_VALUE_ARRAY || !values.a) {
        return cxpr_contains_error(err, CXPR_ERR_TYPE_MISMATCH,
                                   "contains() values argument must be an array");
    }
    for (size_t i = 0u; i < values.a->count; ++i) {
        bool equal = false;
        if (!cxpr_contains_scalar_equal(source, values.a->values[i], &equal)) {
            return cxpr_contains_error(err, CXPR_ERR_TYPE_MISMATCH,
                                       "contains() supports scalar values only");
        }
        if (equal) return cxpr_bool(true);
    }
    return cxpr_bool(false);
}

static cxpr_value cxpr_within_error(cxpr_error* err,
                                    cxpr_error_code code,
                                    const char* message) {
    if (err) {
        err->code = code;
        err->message = message;
    }
    return cxpr_bool(false);
}

static int cxpr_within_set_error(cxpr_error* err,
                                 cxpr_error_code code,
                                 const char* message) {
    (void)cxpr_within_error(err, code, message);
    return 0;
}

static int cxpr_within_param_index(const char* name) {
    if (!name) return -1;
    if (strcmp(name, "source") == 0 || strcmp(name, "value") == 0) return 0;
    if (strcmp(name, "min") == 0 || strcmp(name, "lo") == 0) return 1;
    if (strcmp(name, "max") == 0 || strcmp(name, "hi") == 0) return 2;
    if (strcmp(name, "include_min") == 0) return 3;
    if (strcmp(name, "include_max") == 0) return 4;
    return -1;
}

static int cxpr_within_eval_number(const cxpr_ast* ast,
                                   const cxpr_context* ctx,
                                   const cxpr_registry* reg,
                                   double* out,
                                   cxpr_error* err) {
    cxpr_value value;
    if (!cxpr_eval_ast(ast, ctx, reg, &value, err)) return 0;
    if (value.type != CXPR_VALUE_NUMBER) {
        return cxpr_within_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                                     "within() source, min, and max must be numbers");
    }
    if (out) *out = value.d;
    return 1;
}

static int cxpr_within_eval_bool(const cxpr_ast* ast,
                                 const cxpr_context* ctx,
                                 const cxpr_registry* reg,
                                 bool* out,
                                 cxpr_error* err) {
    cxpr_value value;
    if (!cxpr_eval_ast(ast, ctx, reg, &value, err)) return 0;
    if (value.type != CXPR_VALUE_BOOL) {
        return cxpr_within_set_error(err, CXPR_ERR_TYPE_MISMATCH,
                                     "within() include_min/include_max must be bool");
    }
    if (out) *out = value.b;
    return 1;
}

static cxpr_value cxpr_fn_within(const cxpr_ast* call_ast,
                                 const cxpr_context* ctx,
                                 const cxpr_registry* reg,
                                 void* userdata,
                                 cxpr_error* err) {
    const size_t argc = cxpr_ast_function_argc(call_ast);
    const cxpr_ast* ordered[5] = {0};
    bool used[5] = {false, false, false, false, false};
    bool seen_named = false;
    size_t positional = 0u;
    double source = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    bool include_min = true;
    bool include_max = true;
    bool lower_ok;
    bool upper_ok;

    (void)userdata;
    if (argc < 3u || argc > 5u) {
        return cxpr_within_error(err, CXPR_ERR_WRONG_ARITY,
                                 "within() expects source, min, max, and optional include_min/include_max");
    }

    for (size_t i = 0u; i < argc; ++i) {
        const cxpr_ast* arg = cxpr_ast_function_arg(call_ast, i);
        const char* arg_name = cxpr_ast_function_arg_name(call_ast, i);
        int index;

        if (!arg_name) {
            if (seen_named) {
                return cxpr_within_error(err, CXPR_ERR_SYNTAX,
                                         "Positional arguments cannot follow named arguments");
            }
            if (positional >= 5u) {
                return cxpr_within_error(err, CXPR_ERR_WRONG_ARITY,
                                         "Wrong number of arguments");
            }
            ordered[positional] = arg;
            used[positional] = true;
            ++positional;
            continue;
        }

        seen_named = true;
        index = cxpr_within_param_index(arg_name);
        if (index < 0) {
            return cxpr_within_error(err, CXPR_ERR_SYNTAX,
                                     "Unknown named argument for within()");
        }
        if (used[index]) {
            return cxpr_within_error(err, CXPR_ERR_SYNTAX,
                                     "Duplicate argument for within()");
        }
        ordered[index] = arg;
        used[index] = true;
    }

    if (!ordered[0] || !ordered[1] || !ordered[2]) {
        return cxpr_within_error(err, CXPR_ERR_WRONG_ARITY,
                                 "within() requires source, min, and max");
    }
    if (!cxpr_within_eval_number(ordered[0], ctx, reg, &source, err) ||
        !cxpr_within_eval_number(ordered[1], ctx, reg, &min_value, err) ||
        !cxpr_within_eval_number(ordered[2], ctx, reg, &max_value, err)) {
        return cxpr_bool(false);
    }
    if (ordered[3] && !cxpr_within_eval_bool(ordered[3], ctx, reg, &include_min, err)) {
        return cxpr_bool(false);
    }
    if (ordered[4] && !cxpr_within_eval_bool(ordered[4], ctx, reg, &include_max, err)) {
        return cxpr_bool(false);
    }

    lower_ok = include_min ? source >= min_value : source > min_value;
    upper_ok = include_max ? source <= max_value : source < max_value;
    return cxpr_bool(lower_ok && upper_ok);
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

typedef enum {
    CXPR_DEFAULT_VARIADIC,
    CXPR_DEFAULT_NULLARY,
    CXPR_DEFAULT_UNARY,
    CXPR_DEFAULT_BINARY,
    CXPR_DEFAULT_TERNARY
} cxpr_default_kind;

typedef struct {
    const char* name;
    cxpr_default_kind kind;
    union {
        cxpr_func_ptr variadic;
        double (*nullary)(void);
        double (*unary)(double);
        double (*binary)(double, double);
        double (*ternary)(double, double, double);
    } fn;
    size_t min_args;
    size_t max_args;
} cxpr_default_entry;

static const cxpr_default_entry cxpr_default_entries[] = {
    {"min", CXPR_DEFAULT_VARIADIC, {.variadic = cxpr_min}, 1, 8},
    {"max", CXPR_DEFAULT_VARIADIC, {.variadic = cxpr_max}, 1, 8},
    {"clamp", CXPR_DEFAULT_TERNARY, {.ternary = cxpr_clamp}, 3, 3},
    {"sign", CXPR_DEFAULT_UNARY, {.unary = cxpr_sign}, 1, 1},
    {"add", CXPR_DEFAULT_BINARY, {.binary = cxpr_add}, 2, 2},
    {"sub", CXPR_DEFAULT_BINARY, {.binary = cxpr_sub}, 2, 2},
    {"mul", CXPR_DEFAULT_BINARY, {.binary = cxpr_mul}, 2, 2},
    {"div", CXPR_DEFAULT_BINARY, {.binary = cxpr_div}, 2, 2},
    {"lerp", CXPR_DEFAULT_TERNARY, {.ternary = cxpr_lerp}, 3, 3},
    {"smoothstep", CXPR_DEFAULT_TERNARY, {.ternary = cxpr_smoothstep}, 3, 3},
    {"sigmoid", CXPR_DEFAULT_TERNARY, {.ternary = cxpr_sigmoid}, 3, 3},
    {"abs", CXPR_DEFAULT_UNARY, {.unary = fabs}, 1, 1},
    {"floor", CXPR_DEFAULT_UNARY, {.unary = floor}, 1, 1},
    {"ceil", CXPR_DEFAULT_UNARY, {.unary = ceil}, 1, 1},
    {"round", CXPR_DEFAULT_UNARY, {.unary = round}, 1, 1},
    {"trunc", CXPR_DEFAULT_UNARY, {.unary = trunc}, 1, 1},
    {"sqrt", CXPR_DEFAULT_UNARY, {.unary = sqrt}, 1, 1},
    {"cbrt", CXPR_DEFAULT_UNARY, {.unary = cbrt}, 1, 1},
    {"hypot", CXPR_DEFAULT_BINARY, {.binary = hypot}, 2, 2},
    {"pow", CXPR_DEFAULT_BINARY, {.binary = pow}, 2, 2},
    {"exp", CXPR_DEFAULT_UNARY, {.unary = exp}, 1, 1},
    {"exp2", CXPR_DEFAULT_UNARY, {.unary = exp2}, 1, 1},
    {"expm1", CXPR_DEFAULT_UNARY, {.unary = expm1}, 1, 1},
    {"log", CXPR_DEFAULT_UNARY, {.unary = log}, 1, 1},
    {"log10", CXPR_DEFAULT_UNARY, {.unary = log10}, 1, 1},
    {"log2", CXPR_DEFAULT_UNARY, {.unary = log2}, 1, 1},
    {"log1p", CXPR_DEFAULT_UNARY, {.unary = log1p}, 1, 1},
    {"mod", CXPR_DEFAULT_BINARY, {.binary = fmod}, 2, 2},
    {"copysign", CXPR_DEFAULT_BINARY, {.binary = copysign}, 2, 2},
    {"radians", CXPR_DEFAULT_UNARY, {.unary = cxpr_radians}, 1, 1},
    {"degrees", CXPR_DEFAULT_UNARY, {.unary = cxpr_degrees}, 1, 1},
    {"sin", CXPR_DEFAULT_UNARY, {.unary = sin}, 1, 1},
    {"cos", CXPR_DEFAULT_UNARY, {.unary = cos}, 1, 1},
    {"tan", CXPR_DEFAULT_UNARY, {.unary = tan}, 1, 1},
    {"asin", CXPR_DEFAULT_UNARY, {.unary = asin}, 1, 1},
    {"acos", CXPR_DEFAULT_UNARY, {.unary = acos}, 1, 1},
    {"atan", CXPR_DEFAULT_UNARY, {.unary = atan}, 1, 1},
    {"atan2", CXPR_DEFAULT_BINARY, {.binary = atan2}, 2, 2},
    {"sinh", CXPR_DEFAULT_UNARY, {.unary = sinh}, 1, 1},
    {"cosh", CXPR_DEFAULT_UNARY, {.unary = cosh}, 1, 1},
    {"tanh", CXPR_DEFAULT_UNARY, {.unary = tanh}, 1, 1},
    {"pi", CXPR_DEFAULT_NULLARY, {.nullary = cxpr_pi}, 0, 0},
    {"e", CXPR_DEFAULT_NULLARY, {.nullary = cxpr_e}, 0, 0},
    {"nan", CXPR_DEFAULT_NULLARY, {.nullary = cxpr_nan}, 0, 0},
    {"inf", CXPR_DEFAULT_NULLARY, {.nullary = cxpr_inf}, 0, 0},
    {"if", CXPR_DEFAULT_TERNARY, {.ternary = cxpr_if}, 3, 3},
};

bool cxpr_register_default_named(cxpr_registry* reg, const char* name) {
    if (!reg || !name || cxpr_registry_find(reg, name)) return true;

    for (size_t i = 0; i < CXPR_ARRAY_COUNT(cxpr_default_entries); ++i) {
        const cxpr_default_entry* entry = &cxpr_default_entries[i];
        if (strcmp(name, entry->name) != 0) continue;
        switch (entry->kind) {
        case CXPR_DEFAULT_VARIADIC:
            cxpr_registry_add(reg, entry->name, entry->fn.variadic,
                              entry->min_args, entry->max_args, NULL, NULL);
            break;
        case CXPR_DEFAULT_NULLARY:
            cxpr_registry_add_nullary(reg, entry->name, entry->fn.nullary);
            break;
        case CXPR_DEFAULT_UNARY:
            cxpr_registry_add_unary(reg, entry->name, entry->fn.unary);
            break;
        case CXPR_DEFAULT_BINARY:
            cxpr_registry_add_binary(reg, entry->name, entry->fn.binary);
            break;
        case CXPR_DEFAULT_TERNARY:
            cxpr_registry_add_ternary(reg, entry->name, entry->fn.ternary);
            break;
        }
        return cxpr_registry_find(reg, name) != NULL;
    }

    if (strcmp(name, "contains") == 0) {
        cxpr_registry_add_ast(reg, "contains", cxpr_fn_contains, 2, 2,
                              CXPR_VALUE_BOOL, NULL, NULL);
    } else if (strcmp(name, "within") == 0) {
        cxpr_registry_add_ast(reg, "within", cxpr_fn_within, 3, 5,
                              CXPR_VALUE_BOOL, NULL, NULL);
    } else if (strcmp(name, "isnan") == 0) {
        static const cxpr_value_type number_arg[] = { CXPR_VALUE_NUMBER };
        cxpr_registry_add_typed(reg, "isnan", cxpr_fn_isnan, 1, 1, number_arg,
                                CXPR_VALUE_BOOL, NULL, NULL);
    } else if (strcmp(name, "isfinite") == 0) {
        static const cxpr_value_type number_arg[] = { CXPR_VALUE_NUMBER };
        cxpr_registry_add_typed(reg, "isfinite", cxpr_fn_isfinite, 1, 1, number_arg,
                                CXPR_VALUE_BOOL, NULL, NULL);
    } else if (strcmp(name, "coalesce") == 0) {
        cxpr_registry_add_value(reg, "coalesce", cxpr_fn_coalesce, 1, 8, NULL, NULL);
    } else if (strcmp(name, "is_null") == 0) {
        cxpr_registry_add_typed(reg, "is_null", cxpr_fn_is_null, 1, 1, NULL,
                                CXPR_VALUE_BOOL, NULL, NULL);
    } else if (cxpr_timeseries_is_builtin(name)) {
        cxpr_register_timeseries(reg);
    } else {
        return false;
    }

    return cxpr_registry_find(reg, name) != NULL;
}

void cxpr_register_math(cxpr_registry* reg) {
    if (!reg) return;

    cxpr_registry_add(reg, "min", cxpr_min, 1, 8, NULL, NULL);
    cxpr_registry_add(reg, "max", cxpr_max, 1, 8, NULL, NULL);
    cxpr_registry_add_ternary(reg, "clamp", cxpr_clamp);
    cxpr_registry_add_unary(reg, "sign", cxpr_sign);
    cxpr_registry_add_binary(reg, "add", cxpr_add);
    cxpr_registry_add_binary(reg, "sub", cxpr_sub);
    cxpr_registry_add_binary(reg, "mul", cxpr_mul);
    cxpr_registry_add_binary(reg, "div", cxpr_div);
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
    cxpr_registry_add_binary(reg, "hypot", hypot);
    cxpr_registry_add_binary(reg, "pow", pow);
    cxpr_registry_add_unary(reg, "exp", exp);
    cxpr_registry_add_unary(reg, "exp2", exp2);
    cxpr_registry_add_unary(reg, "expm1", expm1);

    cxpr_registry_add_unary(reg, "log", log);
    cxpr_registry_add_unary(reg, "log10", log10);
    cxpr_registry_add_unary(reg, "log2", log2);
    cxpr_registry_add_unary(reg, "log1p", log1p);

    cxpr_registry_add_binary(reg, "mod", fmod);
    cxpr_registry_add_binary(reg, "copysign", copysign);
    cxpr_registry_add_unary(reg, "radians", cxpr_radians);
    cxpr_registry_add_unary(reg, "degrees", cxpr_degrees);

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
    cxpr_registry_add_ast(reg, "contains", cxpr_fn_contains, 2, 2, CXPR_VALUE_BOOL, NULL, NULL);
    cxpr_registry_add_ast(reg, "within", cxpr_fn_within, 3, 5, CXPR_VALUE_BOOL, NULL, NULL);

    {
        static const cxpr_value_type number_arg[] = { CXPR_VALUE_NUMBER };
        cxpr_registry_add_typed(reg, "isnan", cxpr_fn_isnan, 1, 1, number_arg,
                                CXPR_VALUE_BOOL, NULL, NULL);
        cxpr_registry_add_typed(reg, "isfinite", cxpr_fn_isfinite, 1, 1, number_arg,
                                CXPR_VALUE_BOOL, NULL, NULL);
    }

    cxpr_registry_add_value(reg, "coalesce", cxpr_fn_coalesce, 1, 8, NULL, NULL);
    cxpr_registry_add_typed(reg, "is_null", cxpr_fn_is_null, 1, 1, NULL,
                            CXPR_VALUE_BOOL, NULL, NULL);
}

void cxpr_register_defaults(cxpr_registry* reg) {
    if (!reg) return;
    cxpr_register_math(reg);
    cxpr_register_timeseries(reg);
}
