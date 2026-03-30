/**
 * @file math.test.c
 * @brief Unit tests for mathematical expression evaluation.
 *
 * Tests covered:
 * - Arithmetic operations (+, -, *, /, %, ^)
 * - Parenthesized expressions
 * - Built-in math functions (abs, sqrt, sin, cos, etc.)
 * - Constants (pi, e)
 * - Edge cases (negative numbers, zero, large values)
 * - Unary minus
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)
#define ASSERT_APPROX(a, b, eps) assert(fabs((a) - (b)) < (eps))

static double eval_expr(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    assert(ast && "Parse failed");
    double result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Arithmetic
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_basic_arithmetic(void) {
    ASSERT_DOUBLE_EQ(eval_expr("1 + 2"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("10 - 3"), 7.0);
    ASSERT_DOUBLE_EQ(eval_expr("4 * 5"), 20.0);
    ASSERT_DOUBLE_EQ(eval_expr("20 / 4"), 5.0);
    ASSERT_DOUBLE_EQ(eval_expr("17 % 5"), 2.0);
    printf("  ✓ test_basic_arithmetic\n");
}

static void test_power(void) {
    ASSERT_DOUBLE_EQ(eval_expr("2 ^ 10"), 1024.0);
    ASSERT_DOUBLE_EQ(eval_expr("3 ** 3"), 27.0);
    ASSERT_DOUBLE_EQ(eval_expr("4 ^ 0.5"), 2.0);
    printf("  ✓ test_power\n");
}

static void test_unary_minus(void) {
    ASSERT_DOUBLE_EQ(eval_expr("-5"), -5.0);
    ASSERT_DOUBLE_EQ(eval_expr("-(-3)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("-(2 + 3)"), -5.0);
    printf("  ✓ test_unary_minus\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Parentheses
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_parentheses(void) {
    ASSERT_DOUBLE_EQ(eval_expr("(2 + 3) * 4"), 20.0);
    ASSERT_DOUBLE_EQ(eval_expr("2 + 3 * 4"), 14.0);
    ASSERT_DOUBLE_EQ(eval_expr("((2 + 3) * (4 + 5))"), 45.0);
    ASSERT_DOUBLE_EQ(eval_expr("(((1 + 2)))"), 3.0);
    printf("  ✓ test_parentheses\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Arithmetic
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_arithmetic(void) {
    ASSERT_DOUBLE_EQ(eval_expr("abs(-7)"), 7.0);
    ASSERT_DOUBLE_EQ(eval_expr("abs(3)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("min(3, 7)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("max(3, 7)"), 7.0);
    ASSERT_DOUBLE_EQ(eval_expr("min(9, -2, 4, 11)"), -2.0);
    ASSERT_DOUBLE_EQ(eval_expr("max(9, -2, 4, 11)"), 11.0);
    ASSERT_DOUBLE_EQ(eval_expr("clamp(15, 0, 10)"), 10.0);
    ASSERT_DOUBLE_EQ(eval_expr("clamp(-5, 0, 10)"), 0.0);
    ASSERT_DOUBLE_EQ(eval_expr("clamp(5, 0, 10)"), 5.0);
    ASSERT_DOUBLE_EQ(eval_expr("sign(-3)"), -1.0);
    ASSERT_DOUBLE_EQ(eval_expr("sign(0)"), 0.0);
    ASSERT_DOUBLE_EQ(eval_expr("sign(5)"), 1.0);
    printf("  ✓ test_builtin_arithmetic\n");
}

static void test_builtin_interpolation(void) {
    ASSERT_DOUBLE_EQ(eval_expr("lerp(0, 10, 0.5)"), 5.0);
    ASSERT_DOUBLE_EQ(eval_expr("smoothstep(0, 0, 1)"), 0.0);
    ASSERT_DOUBLE_EQ(eval_expr("smoothstep(1, 0, 1)"), 1.0);
    ASSERT_APPROX(eval_expr("sigmoid(25, 25, 1)"), 0.5, 1e-12);
    printf("  ✓ test_builtin_interpolation\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Rounding
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_rounding(void) {
    ASSERT_DOUBLE_EQ(eval_expr("floor(3.7)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("floor(-3.2)"), -4.0);
    ASSERT_DOUBLE_EQ(eval_expr("ceil(3.2)"), 4.0);
    ASSERT_DOUBLE_EQ(eval_expr("ceil(-3.7)"), -3.0);
    ASSERT_DOUBLE_EQ(eval_expr("round(3.5)"), 4.0);
    ASSERT_DOUBLE_EQ(eval_expr("trunc(3.7)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("trunc(-3.7)"), -3.0);
    printf("  ✓ test_builtin_rounding\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Power/Roots
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_power(void) {
    ASSERT_DOUBLE_EQ(eval_expr("sqrt(9)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("sqrt(2)"), sqrt(2.0));
    ASSERT_DOUBLE_EQ(eval_expr("cbrt(27)"), 3.0);
    ASSERT_DOUBLE_EQ(eval_expr("pow(2, 8)"), 256.0);
    ASSERT_APPROX(eval_expr("exp(1)"), 2.71828182845904523536, 1e-12);
    ASSERT_DOUBLE_EQ(eval_expr("exp2(3)"), 8.0);
    printf("  ✓ test_builtin_power\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Logarithms
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_log(void) {
    ASSERT_APPROX(eval_expr("log(1)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("log(e())"), 1.0, 1e-12);
    ASSERT_DOUBLE_EQ(eval_expr("log10(100)"), 2.0);
    ASSERT_DOUBLE_EQ(eval_expr("log2(8)"), 3.0);
    printf("  ✓ test_builtin_log\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Trigonometric
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_trig(void) {
    ASSERT_APPROX(eval_expr("sin(0)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("cos(0)"), 1.0, EPSILON);
    ASSERT_APPROX(eval_expr("tan(0)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("asin(0)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("acos(1)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("atan(0)"), 0.0, EPSILON);
    ASSERT_APPROX(eval_expr("atan2(0, 1)"), 0.0, EPSILON);
    printf("  ✓ test_builtin_trig\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Constants
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_constants(void) {
    ASSERT_APPROX(eval_expr("pi()"), 3.14159265358979323846, 1e-14);
    ASSERT_APPROX(eval_expr("e()"), 2.71828182845904523536, 1e-14);
    assert(isnan(eval_expr("nan()")));
    assert(isinf(eval_expr("inf()")));
    printf("  ✓ test_builtin_constants\n");
}

static void test_builtin_nan_inf(void) {
    /* nan() doesn't trigger an error — just returns NaN */
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    cxpr_ast* ast = cxpr_parse(p, "nan()", &err);
    assert(ast);
    double result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(isnan(result));

    cxpr_ast_free(ast);
    ast = cxpr_parse(p, "inf()", &err);
    assert(ast);
    result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(isinf(result));

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_builtin_nan_inf\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Built-in functions: Conditional
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_builtin_if(void) {
    ASSERT_DOUBLE_EQ(eval_expr("if(1, 10, 20)"), 10.0);
    ASSERT_DOUBLE_EQ(eval_expr("if(0, 10, 20)"), 20.0);
    ASSERT_DOUBLE_EQ(eval_expr("if(3 > 2, 100, 200)"), 100.0);
    printf("  ✓ test_builtin_if\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Complex mathematical expressions
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_complex_math(void) {
    /* sqrt(x^2 + y^2) with x=3, y=4 → 5 */
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);

    cxpr_ast* ast = cxpr_parse(p, "sqrt(x^2 + y^2)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 5.0);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_complex_math\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Custom functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Weighted average: wavg(value, weight, value, weight, ...) */
static double fn_wavg(const double* a, size_t n, void* u) {
    (void)u;
    double sum = 0.0, wsum = 0.0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        sum += a[i] * a[i + 1];
        wsum += a[i + 1];
    }
    return wsum != 0.0 ? sum / wsum : 0.0;
}

/* Exponential moving average step: ema_step(prev, value, alpha) */
static double fn_ema_step(const double* a, size_t n, void* u) {
    (void)n; (void)u;
    double prev = a[0], value = a[1], alpha = a[2];
    return alpha * value + (1.0 - alpha) * prev;
}

/* Normalize to [0,1]: normalize(x, min, max) */
static double fn_normalize(const double* a, size_t n, void* u) {
    (void)n; (void)u;
    double x = a[0], lo = a[1], hi = a[2];
    if (hi == lo) return 0.0;
    return (x - lo) / (hi - lo);
}

/* Simplified cross_above: returns 1 if a > b, else 0 */
static double fn_cross_above(const double* a, size_t n, void* u) {
    (void)n; (void)u;
    return (a[0] > a[1]) ? 1.0 : 0.0;
}

/* Simplified divergence: returns |a - b| / max(|a|, |b|) */
static double fn_divergence(const double* a, size_t n, void* u) {
    (void)n; (void)u;
    double absA = fabs(a[0]), absB = fabs(a[1]);
    double denom = absA > absB ? absA : absB;
    return denom != 0.0 ? fabs(a[0] - a[1]) / denom : 0.0;
}

static void test_custom_functions(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    cxpr_registry_add(reg, "wavg", fn_wavg, 2, 10, NULL, NULL);
    cxpr_registry_add(reg, "ema_step", fn_ema_step, 3, 3, NULL, NULL);
    cxpr_registry_add(reg, "normalize", fn_normalize, 3, 3, NULL, NULL);
    cxpr_registry_add(reg, "cross_above", fn_cross_above, 2, 2, NULL, NULL);
    cxpr_registry_add(reg, "divergence", fn_divergence, 2, 2, NULL, NULL);

    /* wavg(80, 0.5, 90, 0.3, 70, 0.2) = (40+27+14)/1.0 = 81.0 */
    cxpr_ast* ast = cxpr_parse(p, "wavg(80, 0.5, 90, 0.3, 70, 0.2)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 81.0);
    cxpr_ast_free(ast);

    /* ema_step(100, 105, 0.2) = 0.2*105 + 0.8*100 = 21 + 80 = 101 */
    ast = cxpr_parse(p, "ema_step(100, 105, 0.2)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 101.0);
    cxpr_ast_free(ast);

    /* normalize(75, 0, 100) = 0.75 */
    ast = cxpr_parse(p, "normalize(75, 0, 100)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 0.75);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_custom_functions\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Extreme complexity: deeply nested, mixed builtins + custom + variables
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_extreme_nested_expressions(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};
    cxpr_ast* ast;

    cxpr_registry_add(reg, "cross_above", fn_cross_above, 2, 2, NULL, NULL);
    cxpr_registry_add(reg, "normalize", fn_normalize, 3, 3, NULL, NULL);
    cxpr_registry_add(reg, "divergence", fn_divergence, 2, 2, NULL, NULL);
    cxpr_registry_add(reg, "ema_step", fn_ema_step, 3, 3, NULL, NULL);

    /* ── Test 1: nested functions inside functions ── */
    /* clamp(sqrt(abs(-16)) * 2, 0, 10) = clamp(4*2, 0, 10) = clamp(8,0,10) = 8 */
    ast = cxpr_parse(p, "clamp(sqrt(abs(-16)) * 2, 0, 10)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 8.0);
    cxpr_ast_free(ast);

    /* ── Test 2: triple-nested function with arithmetic ── */
    /* max(min(abs(-7) + 3, 20), sqrt(pow(3, 2) + pow(4, 2)))
       = max(min(10, 20), sqrt(9+16)) = max(10, 5) = 10 */
    ast = cxpr_parse(p, "max(min(abs(-7) + 3, 20), sqrt(pow(3, 2) + pow(4, 2)))", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 10.0);
    cxpr_ast_free(ast);

    /* ── Test 3: trig identity sin²(x) + cos²(x) = 1 ── */
    cxpr_context_set(ctx, "x", 1.2345);
    ast = cxpr_parse(p, "sin(x)^2 + cos(x)^2", &err);
    assert(ast);
    ASSERT_APPROX(cxpr_ast_eval(ast, ctx, reg, &err), 1.0, 1e-12);
    cxpr_ast_free(ast);

    /* ── Test 4: complex ternary chain with comparisons ── */
    /* signal classification: oversold/neutral/overbought */
    cxpr_context_set(ctx, "rsi", 22.0);
    cxpr_context_set_param(ctx, "low", 30.0);
    cxpr_context_set_param(ctx, "high", 70.0);
    ast = cxpr_parse(p, "rsi < $low ? -1.0 : (rsi > $high ? 1.0 : 0.0)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), -1.0);
    cxpr_ast_free(ast);

    cxpr_context_set(ctx, "rsi", 50.0);
    ast = cxpr_parse(p, "rsi < $low ? -1.0 : (rsi > $high ? 1.0 : 0.0)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 0.0);
    cxpr_ast_free(ast);

    cxpr_context_set(ctx, "rsi", 85.0);
    ast = cxpr_parse(p, "rsi < $low ? -1.0 : (rsi > $high ? 1.0 : 0.0)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 1.0);
    cxpr_ast_free(ast);

    /* ── Test 5: many variables + builtins + custom in one expression ── */
    /* Complex signal scoring example
       normalize(rsi, 0, 100) < 0.3 and cross_above(ema_f, ema_s)
       and divergence(macd_h, prev_macd_h) < 0.5 */
    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set(ctx, "ema_f", 101.5);
    cxpr_context_set(ctx, "ema_s", 100.2);
    cxpr_context_set(ctx, "macd_h", 0.3);
    cxpr_context_set(ctx, "prev_macd_h", 0.35);
    ast = cxpr_parse(p,
        "normalize(rsi, 0, 100) < 0.3 and cross_above(ema_f, ema_s) and divergence(macd_h, prev_macd_h) < 0.5",
        &err);
    assert(ast);
    assert(cxpr_ast_eval_bool(ast, ctx, reg, &err) == true);
    cxpr_ast_free(ast);

    /* ── Test 6: deeply nested arithmetic + functions (6 levels deep) ── */
    /* floor(sqrt(abs(min(-100, max(3, pow(2, ceil(log2(15)))))))) 
       log2(15) ≈ 3.907 → ceil = 4 → pow(2,4) = 16
       max(3, 16) = 16 → min(-100, 16) = -100
       abs(-100) = 100 → sqrt(100) = 10 → floor(10) = 10 */
    ast = cxpr_parse(p, "floor(sqrt(abs(min(-100, max(3, pow(2, ceil(log2(15))))))))", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 10.0);
    cxpr_ast_free(ast);

    /* ── Test 7: chained EMA steps with intermediate math ── */
    /* ema_step(ema_step(100, 110, 0.1), 105, 0.1)
       inner: 0.1*110 + 0.9*100 = 11+90 = 101
       outer: 0.1*105 + 0.9*101 = 10.5+90.9 = 101.4 */
    ast = cxpr_parse(p, "ema_step(ema_step(100, 110, 0.1), 105, 0.1)", &err);
    assert(ast);
    ASSERT_APPROX(cxpr_ast_eval(ast, ctx, reg, &err), 101.4, 1e-10);
    cxpr_ast_free(ast);

    /* ── Test 8: boolean logic with nested comparisons ── */
    /* (rsi < 30 or (rsi < 40 and cross_above(ema_f, ema_s)))
       and not (macd_h < 0 and prev_macd_h < 0) */
    cxpr_context_set(ctx, "rsi", 35.0);
    cxpr_context_set(ctx, "macd_h", 0.3);
    cxpr_context_set(ctx, "prev_macd_h", -0.1);
    ast = cxpr_parse(p,
        "(rsi < 30 or (rsi < 40 and cross_above(ema_f, ema_s))) and not (macd_h < 0 and prev_macd_h < 0)",
        &err);
    assert(ast);
    /* rsi=35: 35<30=false, (35<40=true and cross=true)=true → or=true
       macd_h=0.3>0: (0.3<0=false and ...)=false → not false=true
       true and true = true */
    assert(cxpr_ast_eval_bool(ast, ctx, reg, &err) == true);
    cxpr_ast_free(ast);

    /* ── Test 9: compound math expression (quadratic formula) ── */
    /* (-b + sqrt(b^2 - 4*a*c)) / (2*a) with a=1, b=-5, c=6 → x=3 */
    cxpr_context_set(ctx, "a", 1.0);
    cxpr_context_set(ctx, "b", -5.0);
    cxpr_context_set(ctx, "c", 6.0);
    ast = cxpr_parse(p, "(-b + sqrt(b^2 - 4*a*c)) / (2*a)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 3.0);
    cxpr_ast_free(ast);

    /* and the other root: x=2 */
    ast = cxpr_parse(p, "(-b - sqrt(b^2 - 4*a*c)) / (2*a)", &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 2.0);
    cxpr_ast_free(ast);

    /* ── Test 10: ternary inside function inside ternary ── */
    /* if(x > 0, sqrt(x), abs(x)) where x = -9 → abs(-9) = 9
       then: max(result, if(y > 0, y^2, -y)) where y = -3 → -(-3)=3
       full: max(if(-9 > 0, sqrt(-9), abs(-9)), if(-3 > 0, (-3)^2, -(-3)))
             max(9, 3) = 9 */
    cxpr_context_set(ctx, "x", -9.0);
    cxpr_context_set(ctx, "y", -3.0);
    ast = cxpr_parse(p,
        "max(if(x > 0, sqrt(x), abs(x)), if(y > 0, y ^ 2, -y))",
        &err);
    assert(ast);
    ASSERT_DOUBLE_EQ(cxpr_ast_eval(ast, ctx, reg, &err), 9.0);
    cxpr_ast_free(ast);

    /* ── Test 11: long chain of and/or with comparison and arithmetic ── */
    cxpr_context_set(ctx, "v1", 10.0);
    cxpr_context_set(ctx, "v2", 20.0);
    cxpr_context_set(ctx, "v3", 30.0);
    cxpr_context_set(ctx, "v4", 40.0);
    cxpr_context_set(ctx, "v5", 50.0);
    ast = cxpr_parse(p,
        "v1 + v2 > 25 and v3 * 2 == 60 and (v4 - v5 < 0 or v1 ^ 2 == 100) and not (v2 > 100) and min(v1, v5) == 10 and max(v3, v4) == 40",
        &err);
    assert(ast);
    assert(cxpr_ast_eval_bool(ast, ctx, reg, &err) == true);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_extreme_nested_expressions\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running math tests...\n");
    test_basic_arithmetic();
    test_power();
    test_unary_minus();
    test_parentheses();
    test_builtin_arithmetic();
    test_builtin_interpolation();
    test_builtin_rounding();
    test_builtin_power();
    test_builtin_log();
    test_builtin_trig();
    test_builtin_constants();
    test_builtin_nan_inf();
    test_builtin_if();
    test_complex_math();
    test_custom_functions();
    test_extreme_nested_expressions();
    printf("All math tests passed!\n");
    return 0;
}
