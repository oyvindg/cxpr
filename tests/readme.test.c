/**
 * @file readme.test.c
 * @brief Executable tests for the short README snippets and related doc examples.
 *
 * Each test is a minimal, self-contained reproduction of a documented usage
 * pattern so that the public examples and the working code stay aligned.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "cxpr_test_internal.h"

#define EPSILON 1e-10
#define READMETEST_PI 3.14159265358979323846
#define ASSERT_APPROX(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Quick Start
 *
 *   cxpr_context_set(ctx, "angle_deg", 120.0);
 *   cxpr_context_set_param(ctx, "limit", 1.2);
 *   cxpr_registry_add_unary(reg, "deg2rad", readme_deg2rad);
 *   cxpr_registry_add_ternary(reg, "clamp", readme_clamp);
 *   cxpr_registry_add_value(reg, "within_limit", readme_within_limit, 2, 2, NULL, NULL);
 *   cxpr_ast* ast = cxpr_parse(parser,
 *       "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)", &err);
 * ═══════════════════════════════════════════════════════════════════════════ */

static double readme_deg2rad(double d)                     { return d * READMETEST_PI / 180.0; }
static double readme_clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static cxpr_value readme_within_limit(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_fv_bool(args[0] < args[1]);
}

static void test_readme_quick_start(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "angle_deg", 30.0);
    cxpr_context_set_param(ctx, "limit", 1.2);
    cxpr_registry_add_unary(reg, "deg2rad", readme_deg2rad);
    cxpr_registry_add_ternary(reg, "clamp", readme_clamp);
    cxpr_registry_add_value(reg, "within_limit", readme_within_limit, 2, 2, NULL, NULL);

    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser,
        "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)", &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    cxpr_value result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true); /* 30deg ~= 0.52rad < 1.2 */

    cxpr_context_set(ctx, "angle_deg", 120.0);
    result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == false); /* clamp(pi*120/180, 0, 1.57) = 1.57 >= 1.2 */

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_quick_start\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Repeated evaluation — compile once, evaluate many times
 *
 *   cxpr_program* prog = cxpr_compile(ast, reg, &err);
 *   cxpr_value fast_result = cxpr_test_eval_program(prog, ctx, reg, &err);
 *
 * Verifies that AST and IR paths agree on every context update.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_ir_path(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    cxpr_registry_add_unary(reg, "deg2rad", readme_deg2rad);
    cxpr_registry_add_ternary(reg, "clamp", readme_clamp);
    cxpr_registry_add_value(reg, "within_limit", readme_within_limit, 2, 2, NULL, NULL);

    cxpr_ast* ast = cxpr_parse(parser,
        "within_limit(clamp(deg2rad(angle_deg), 0.0, 1.57), $limit)", &err);
    assert(ast);
    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    cxpr_context_set_param(ctx, "limit", 1.2);

    cxpr_context_set(ctx, "angle_deg", 30.0);
    cxpr_value ast_result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    cxpr_value ir_result  = cxpr_test_eval_program(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(ast_result.type == CXPR_VALUE_BOOL);
    assert(ir_result.type == CXPR_VALUE_BOOL);
    assert(ast_result.b == true);
    assert(ir_result.b  == ast_result.b);

    cxpr_context_set(ctx, "angle_deg", 120.0);
    ast_result = cxpr_test_eval_ast(ast, ctx, reg, &err);
    ir_result  = cxpr_test_eval_program(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(ast_result.type == CXPR_VALUE_BOOL);
    assert(ir_result.type == CXPR_VALUE_BOOL);
    assert(ast_result.b == false);
    assert(ir_result.b  == ast_result.b);

    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_ir_path\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Custom C functions — convenience wrappers
 *
 *   cxpr_registry_add_unary(reg, "deg2rad", deg2rad);
 *   cxpr_registry_add_ternary(reg, "clamp", clamp);
 *   cxpr_registry_add_value(reg, "within_limit", within_limit, 2, 2, NULL, NULL);
 * ═══════════════════════════════════════════════════════════════════════════ */

static double readme_rand_uniform(void)                        { return 0.25; }

static void test_readme_custom_c_functions(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_registry_add_unary(reg,   "deg2rad",      readme_deg2rad);
    cxpr_registry_add_ternary(reg, "clamp",        readme_clamp);
    cxpr_registry_add_value(reg,   "within_limit", readme_within_limit, 2, 2, NULL, NULL);
    cxpr_registry_add_nullary(reg, "rand_uniform", readme_rand_uniform);

    cxpr_error err = {0};

#define EVAL_DOUBLE(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); \
    double _r = cxpr_test_eval_ast_number(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    cxpr_ast_free(_a); \
    _r; \
})

    ASSERT_APPROX(EVAL_DOUBLE("deg2rad(180)"), READMETEST_PI);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(15, 0, 10)"), 10.0);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(-5, 0, 10)"),  0.0);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(5, 0, 10)"),   5.0);
    ASSERT_APPROX(EVAL_DOUBLE("rand_uniform()"),    0.25);

#define EVAL_BOOL(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); \
    cxpr_value _r = cxpr_test_eval_ast(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    assert(_r.type == CXPR_VALUE_BOOL); \
    cxpr_ast_free(_a); \
    _r.b; \
})

    assert(EVAL_BOOL("within_limit(0.5, 1.2)") == true);
    assert(EVAL_BOOL("within_limit(1.57, 1.2)") == false);

#undef EVAL_BOOL
#undef EVAL_DOUBLE

    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_custom_c_functions\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Custom C functions — general form with userdata
 *
 *   static double fn_lookup(const double* args, size_t argc, void* userdata) {
 *       MyTable* table = (MyTable*)userdata;
 *       return mytable_lookup(table, (int)args[0]);
 *   }
 *   cxpr_registry_add(reg, "lookup", fn_lookup, 1, 1, my_table, NULL);
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct { double values[4]; } readme_table_t;

static double fn_lookup(const double* args, size_t argc, void* userdata) {
    (void)argc;
    const readme_table_t* tbl = (const readme_table_t*)userdata;
    int idx = (int)args[0];
    return tbl->values[idx];
}

static void test_readme_custom_fn_with_userdata(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    readme_table_t* tbl = (readme_table_t*)malloc(sizeof(readme_table_t));
    assert(tbl);
    tbl->values[0] = 10.0;
    tbl->values[1] = 20.0;
    tbl->values[2] = 30.0;
    tbl->values[3] = 40.0;

    cxpr_registry_add(reg, "lookup", fn_lookup, 1, 1, tbl, free);

    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "lookup(2)", &err);
    assert(ast);
    ASSERT_APPROX(cxpr_test_eval_ast_number(ast, ctx, reg, &err), 30.0);
    assert(err.code == CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);  /* calls free(tbl) via free_userdata */
    printf("  ✓ test_readme_custom_fn_with_userdata\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Expression-defined functions — scalar params
 *
 *   cxpr_registry_define_fn(reg, "sq(x) => x * x");
 *   cxpr_registry_define_fn(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))");
 *   cxpr_registry_define_fn(reg, "clamp_score(x) => x > 1 ? 1 : (x < -1 ? -1 : x)");
 *   cxpr_registry_define_fn(reg, "sum(a, b) => a + b");
 *   cxpr_registry_define_fn(reg, "lerp(a, b, t) => a + (b - a) * t");
 *   cxpr_registry_define_fn(reg, "energy(m, v) => 0.5 * m * v * v");
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_define_scalar(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    assert(cxpr_registry_define_fn(reg, "sq(x) => x * x").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "hyp2(a, b) => sqrt(sq(a) + sq(b))").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "clamp_score(x) => x > 1 ? 1 : (x < -1 ? -1 : x)").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "sum(a, b) => a + b").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "lerp(a, b, t) => a + (b - a) * t").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "energy(m, v) => 0.5 * m * v * v").code == CXPR_OK);

    cxpr_error err = {0};

#define EVAL(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); \
    double _r = cxpr_test_eval_ast_number(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    cxpr_ast_free(_a); \
    _r; \
})

    ASSERT_APPROX(EVAL("sq(5)"),               25.0);
    ASSERT_APPROX(EVAL("hyp2(3, 4)"),           5.0);
    ASSERT_APPROX(EVAL("clamp_score(2.0)"),     1.0);
    ASSERT_APPROX(EVAL("clamp_score(-3.0)"),   -1.0);
    ASSERT_APPROX(EVAL("clamp_score(0.5)"),     0.5);
    ASSERT_APPROX(EVAL("sum(7, 3)"),           10.0);
    ASSERT_APPROX(EVAL("lerp(0, 10, 0.25)"),    2.5);
    ASSERT_APPROX(EVAL("energy(2, 3)"),          9.0);  /* 0.5 * 2 * 9 */

#undef EVAL

    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_define_scalar\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Expression-defined functions — struct params
 *
 *   cxpr_registry_define_fn(reg, "dot2(u, v) => u.x * v.x + u.y * v.y");
 *   cxpr_registry_define_fn(reg, "len2(p) => sqrt(p.x * p.x + p.y * p.y)");
 *
 *   // Usage:
 *   dot2(goal, velocity)
 *   len2(body)
 *
 * Struct fields are populated with cxpr_context_set_fields.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_define_struct(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_parser*  parser = cxpr_parser_new();

    assert(cxpr_registry_define_fn(reg, "dot2(u, v) => u.x * v.x + u.y * v.y").code == CXPR_OK);
    assert(cxpr_registry_define_fn(reg, "len2(p) => sqrt(p.x * p.x + p.y * p.y)").code == CXPR_OK);

    const char* xy[] = {"x", "y"};

    double goal_vals[]     = {3.0, 4.0};
    double velocity_vals[] = {1.0, 0.0};
    double body_vals[]     = {3.0, 4.0};

    cxpr_context_set_fields(ctx, "goal",     xy, goal_vals,     2);
    cxpr_context_set_fields(ctx, "velocity", xy, velocity_vals, 2);
    cxpr_context_set_fields(ctx, "body",     xy, body_vals,     2);

    cxpr_error err = {0};

#define EVAL(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); \
    double _r = cxpr_test_eval_ast_number(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    cxpr_ast_free(_a); \
    _r; \
})

    ASSERT_APPROX(EVAL("dot2(goal, velocity)"), 3.0);  /* 3*1 + 4*0 */
    ASSERT_APPROX(EVAL("len2(body)"),           5.0);  /* sqrt(9 + 16) */

#undef EVAL

    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_define_struct\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Expression Evaluator
 *
 *   cxpr_registry_add_struct(reg, "quote2", quote2_producer, 2, 2, fields, 2, NULL, NULL);
 *   cxpr_expressions_add(evaluator, defs, 4, &err);
 *   cxpr_evaluator_compile(evaluator, &err);
 *   cxpr_evaluator_eval(evaluator, ctx, &err);
 *   cxpr_value quote = cxpr_expression_get(evaluator, "quote", NULL);    // struct
 *   cxpr_value entry = cxpr_expression_get(evaluator, "entry", NULL);    // bool
 *   double score = cxpr_expression_get_double(evaluator, "score", NULL);       // double
 * ═══════════════════════════════════════════════════════════════════════════ */

static void readme_quote2_producer(const double* args, size_t argc,
                                   cxpr_value* out, size_t field_count,
                                   void* userdata) {
    (void)argc;
    (void)field_count;
    (void)userdata;
    out[0] = cxpr_fv_double((args[0] + args[1]) / 2.0);  /* mid */
    out[1] = cxpr_fv_double(args[1] - args[0]);          /* spread */
}

static void test_readme_formula_engine(void) {
    cxpr_registry*      reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context*        ctx    = cxpr_context_new();
    cxpr_error err = {0};

    /* ask=100.5, bid=99.5 → mid=100.0, spread=1.0 */
    cxpr_context_set(ctx, "ask", 100.5);
    cxpr_context_set(ctx, "bid",  99.5);
    cxpr_context_set_param(ctx, "threshold", 0.005);  /* 0.5% spread threshold */
    cxpr_context_set_param(ctx, "min_mid",   99.0);

    const char* fields[] = {"mid", "spread"};
    const cxpr_expression_def defs[] = {
        { "quote", "quote2(bid, ask)" },
        { "wide",  "quote.spread > $threshold" },
        { "entry", "wide and quote.mid > $min_mid" },
        { "score", "quote.mid + quote.spread * 10" }
    };

    cxpr_registry_add_struct(reg, "quote2", readme_quote2_producer,
                             2, 2, fields, 2, NULL, NULL);
    assert(cxpr_expressions_add(evaluator, defs, 4, &err));

    assert(cxpr_evaluator_compile(evaluator, &err));
    assert(err.code == CXPR_OK);

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    cxpr_value quote = cxpr_expression_get(evaluator, "quote", &found);
    assert(found);
    assert(quote.type == CXPR_VALUE_STRUCT);
    assert(quote.s != NULL);
    ASSERT_APPROX(quote.s->field_values[0].d, 100.0);
    ASSERT_APPROX(quote.s->field_values[1].d,   1.0);
    assert(cxpr_expression_get_bool(evaluator, "wide",  &found) == true);   assert(found);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) == true);   assert(found);
    ASSERT_APPROX(cxpr_expression_get_double(evaluator, "score", &found), 110.0); assert(found);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_formula_engine\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Domain — Trading signal composition
 *
 *   cxpr_expression_add(evaluator, "trend",   "close > ema_fast and ema_fast > ema_slow", ...);
 *   cxpr_expression_add(evaluator, "pullback","close < ema_fast * 0.995", ...);
 *   cxpr_expression_add(evaluator, "entry",   "trend and pullback and rsi > 50", ...);
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_domain_trading(void) {
    cxpr_registry*       reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context*        ctx    = cxpr_context_new();
    cxpr_error err = {0};

    /* Example market state from the README expressions */
    cxpr_context_set(ctx, "close",    100.0);
    cxpr_context_set(ctx, "ema_fast", 99.0);
    cxpr_context_set(ctx, "ema_slow", 98.0);
    cxpr_context_set(ctx, "rsi",      55.0);

    /* These three expressions each produce typed boolean results */
    assert(cxpr_expression_add(evaluator, "trend",    "close > ema_fast and ema_fast > ema_slow", &err));
    assert(cxpr_expression_add(evaluator, "pullback", "close < ema_fast * 0.995", &err));
    assert(cxpr_expression_add(evaluator, "entry",
                            "trend and pullback and rsi > 50", &err));

    assert(cxpr_evaluator_compile(evaluator, &err));
    assert(err.code == CXPR_OK);

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    assert(cxpr_expression_get_bool(evaluator, "trend",    &found) == true);  assert(found);
    assert(cxpr_expression_get_bool(evaluator, "pullback", &found) == false); assert(found);
    assert(cxpr_expression_get_bool(evaluator, "entry",    &found) == false); assert(found);

    /* No signal when RSI is too low */
    cxpr_context_set(ctx, "rsi", 40.0);
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(cxpr_expression_get_bool(evaluator, "entry", NULL) == false);

    cxpr_evaluator_free(evaluator);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_domain_trading\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Domain — Robotics guard conditions
 *
 *   cxpr_context_set(ctx, "distance_front", 0.42);
 *   cxpr_context_set_param(ctx, "stop_distance", 0.25);
 *   // "distance_front < $stop_distance ? 0.0 : max_speed * (battery > 20)"
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_domain_robotics(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "distance_front", 0.42);
    cxpr_context_set(ctx, "battery",        76.0);
    cxpr_context_set(ctx, "max_speed",       2.0);
    cxpr_context_set(ctx, "slip_ratio",      0.03);
    cxpr_context_set_param(ctx, "stop_distance", 0.25);
    cxpr_context_set_param(ctx, "max_slip",      0.10);

    cxpr_error err = {0};
    cxpr_ast* stop_expr = cxpr_parse(parser,
        "distance_front < $stop_distance ? 0.0 : (battery > 20 ? max_speed : 0.0)", &err);
    assert(stop_expr);

    /* Clear path: distance_front=0.42 > stop_distance=0.25 → run at max_speed */
    double cmd_vel = cxpr_test_eval_ast_number(stop_expr, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(cmd_vel, 2.0);  /* max_speed * 1 */

    /* Obstacle close: distance_front < stop_distance → stop */
    cxpr_context_set(ctx, "distance_front", 0.20);
    cmd_vel = cxpr_test_eval_ast_number(stop_expr, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(cmd_vel, 0.0);

    cxpr_ast* slip_guard = cxpr_parse(parser,
        "slip_ratio > $max_slip", &err);
    assert(slip_guard);
    assert(cxpr_test_eval_ast_bool(slip_guard, ctx, reg, &err) == false);

    cxpr_ast_free(stop_expr);
    cxpr_ast_free(slip_guard);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_domain_robotics\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Domain — Struct-aware distance helper
 *
 *   cxpr_registry_add_fn(reg, "distance2", fn_distance2, xy, 2, 2, NULL, NULL);
 *   // "distance2(goal, pose) < $capture_radius"
 *
 *   cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);
 *   // "distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z) < $capture_radius"
 * ═══════════════════════════════════════════════════════════════════════════ */

static double fn_distance2(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    double dx = args[0]-args[2], dy = args[1]-args[3];
    return sqrt(dx*dx + dy*dy);
}

static double fn_distance3(const double* args, size_t argc, void* ud) {
    (void)argc; (void)ud;
    double dx = args[0]-args[3], dy = args[1]-args[4], dz = args[2]-args[5];
    return sqrt(dx*dx + dy*dy + dz*dz);
}

static void test_readme_domain_distance(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    const char* xy[]  = {"x", "y"};
    cxpr_registry_add_fn(reg, "distance2", fn_distance2, xy, 2, 2, NULL, NULL);
    cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);

    /* 3-D version via flat args */
    cxpr_context_set(ctx, "goal.x",  4.0);
    cxpr_context_set(ctx, "goal.y",  0.0);
    cxpr_context_set(ctx, "goal.z",  0.0);
    cxpr_context_set(ctx, "pose.x",  0.0);
    cxpr_context_set(ctx, "pose.y",  0.0);
    cxpr_context_set(ctx, "pose.z",  0.0);
    cxpr_context_set_param(ctx, "capture_radius", 5.0);

    cxpr_error err = {0};
    cxpr_ast* ast3 = cxpr_parse(parser,
        "distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z) < $capture_radius", &err);
    assert(ast3);
    assert(cxpr_test_eval_ast_bool(ast3, ctx, reg, &err) == true);   /* dist=4 < 5 */

    /* 2-D version via struct-aware C callback */
    double goal_vals[] = {3.0, 0.0};
    double pose_vals[] = {0.0, 4.0};
    cxpr_context_set_fields(ctx, "goal", xy, goal_vals, 2);
    cxpr_context_set_fields(ctx, "pose", xy, pose_vals, 2);

    cxpr_ast* ast2 = cxpr_parse(parser, "distance2(goal, pose) < $capture_radius", &err);
    assert(ast2);
    assert(cxpr_test_eval_ast_bool(ast2, ctx, reg, &err) == false);  /* dist=5 < 5 is false */

    /* Exact boundary */
    cxpr_context_set_param(ctx, "capture_radius", 5.0);
    bool in_range = cxpr_test_eval_ast_bool(ast2, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(in_range == false);  /* dist=5.0 is not < 5.0 */

    cxpr_ast_free(ast3);
    cxpr_ast_free(ast2);
    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_domain_distance\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Domain — Physics / simulation
 *
 *   cxpr_context_set_fields(ctx, "body", fields, vals, 4);
 *   // "sqrt(body.vx^2 + body.vy^2)"
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_domain_physics(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    const char* body_fields[] = {"x", "y", "vx", "vy"};
    double body_vals[]        = {1.2, -0.5, 3.0, 4.0};
    cxpr_context_set_fields(ctx, "body", body_fields, body_vals, 4);

    cxpr_context_set(ctx, "mass",    2.0);
    cxpr_context_set(ctx, "velocity", 3.0);
    cxpr_context_set_param(ctx, "max_acceleration", 10.0);

    cxpr_error err = {0};

#define EVAL_DOUBLE(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); assert(err.code == CXPR_OK); \
    double _r = cxpr_test_eval_ast_number(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    cxpr_ast_free(_a); \
    _r; \
})

    ASSERT_APPROX(EVAL_DOUBLE("sqrt(body.vx^2 + body.vy^2)"), 5.0);   /* speed = 5 */
    ASSERT_APPROX(EVAL_DOUBLE("0.5 * mass * velocity^2"),      9.0);   /* KE */

#undef EVAL_DOUBLE

    cxpr_parser_free(parser);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_domain_physics\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("Running readme examples tests...\n");
    test_readme_quick_start();
    test_readme_ir_path();
    test_readme_custom_c_functions();
    test_readme_custom_fn_with_userdata();
    test_readme_define_scalar();
    test_readme_define_struct();
    test_readme_formula_engine();
    test_readme_domain_trading();
    test_readme_domain_robotics();
    test_readme_domain_distance();
    test_readme_domain_physics();
    printf("All readme examples tests passed!\n");
    return 0;
}
