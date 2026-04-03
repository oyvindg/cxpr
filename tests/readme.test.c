/**
 * @file readme.test.c
 * @brief Executable tests for every code example in README.md.
 *
 * Each test is a minimal, self-contained reproduction of the corresponding
 * README section so that the documentation and the working code stay in sync.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define EPSILON 1e-10
#define READMETEST_PI 3.14159265358979323846
#define ASSERT_APPROX(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Quick Start
 *
 *   cxpr_context_set(ctx, "rsi", 25.0);
 *   cxpr_context_set(ctx, "volume", 1500000.0);
 *   cxpr_context_set_param(ctx, "min_volume", 1000000.0);
 *   cxpr_ast* ast = cxpr_parse(parser, "rsi < 30 and volume > $min_volume", &err);
 *   cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_quick_start(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_context_set(ctx, "rsi", 25.0);
    cxpr_context_set(ctx, "volume", 1500000.0);
    cxpr_context_set_param(ctx, "min_volume", 1000000.0);

    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, "rsi < 30 and volume > $min_volume", &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_FIELD_BOOL);
    assert(result.b == true); /* rsi=25 < 30, volume=1.5M > 1M */

    /* Also works when the condition is false */
    cxpr_context_set(ctx, "rsi", 35.0);
    result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_FIELD_BOOL);
    assert(result.b == false); /* rsi=35 is not < 30 */

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
 *   cxpr_field_value fast_result = cxpr_ir_eval(prog, ctx, reg, &err);
 *
 * Verifies that AST and IR paths agree on every context update.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_ir_path(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_error err = {0};

    /* Expression from the README IR section */
    cxpr_ast* ast = cxpr_parse(parser, "sqrt(x*x + y*y) > $limit", &err);
    assert(ast);
    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    assert(err.code == CXPR_OK);

    cxpr_context_set_param(ctx, "limit", 4.0);

    /* (3,4): magnitude 5 > 4 → true */
    cxpr_context_set(ctx, "x", 3.0);
    cxpr_context_set(ctx, "y", 4.0);
    cxpr_field_value ast_result = cxpr_ast_eval(ast, ctx, reg, &err);
    cxpr_field_value ir_result  = cxpr_ir_eval(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(ast_result.type == CXPR_FIELD_BOOL);
    assert(ir_result.type == CXPR_FIELD_BOOL);
    assert(ast_result.b == true);
    assert(ir_result.b  == ast_result.b);

    /* (1,1): magnitude ~1.41 < 4 → false */
    cxpr_context_set(ctx, "x", 1.0);
    cxpr_context_set(ctx, "y", 1.0);
    ast_result = cxpr_ast_eval(ast, ctx, reg, &err);
    ir_result  = cxpr_ir_eval(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(ast_result.type == CXPR_FIELD_BOOL);
    assert(ir_result.type == CXPR_FIELD_BOOL);
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
 *   cxpr_registry_add_unary(reg,   "deg2rad",      deg2rad);
 *   cxpr_registry_add_ternary(reg, "clamp",        clamp);
 *   cxpr_registry_add_nullary(reg, "rand_uniform", rand_uniform);
 * ═══════════════════════════════════════════════════════════════════════════ */

static double readme_deg2rad(double d)                         { return d * READMETEST_PI / 180.0; }
static double readme_clamp(double v, double lo, double hi)     { return v < lo ? lo : v > hi ? hi : v; }
static double readme_rand_uniform(void)                        { return 0.25; }

static void test_readme_custom_c_functions(void) {
    cxpr_parser*   parser = cxpr_parser_new();
    cxpr_context*  ctx    = cxpr_context_new();
    cxpr_registry* reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);

    cxpr_registry_add_unary(reg,   "deg2rad",      readme_deg2rad);
    cxpr_registry_add_ternary(reg, "clamp",        readme_clamp);
    cxpr_registry_add_nullary(reg, "rand_uniform", readme_rand_uniform);

    cxpr_error err = {0};

#define EVAL_DOUBLE(expr) ({ \
    cxpr_ast* _a = cxpr_parse(parser, (expr), &err); \
    assert(_a); \
    double _r = cxpr_ast_eval_double(_a, ctx, reg, &err); \
    assert(err.code == CXPR_OK); \
    cxpr_ast_free(_a); \
    _r; \
})

    ASSERT_APPROX(EVAL_DOUBLE("deg2rad(180)"), READMETEST_PI);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(15, 0, 10)"), 10.0);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(-5, 0, 10)"),  0.0);
    ASSERT_APPROX(EVAL_DOUBLE("clamp(5, 0, 10)"),   5.0);
    ASSERT_APPROX(EVAL_DOUBLE("rand_uniform()"),    0.25);

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
    ASSERT_APPROX(cxpr_ast_eval_double(ast, ctx, reg, &err), 30.0);
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
    double _r = cxpr_ast_eval_double(_a, ctx, reg, &err); \
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
    double _r = cxpr_ast_eval_double(_a, ctx, reg, &err); \
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
 * README: Formula Engine
 *
 *   cxpr_formula_add(engine, "spread", "ask - bid",                  &err);
 *   cxpr_formula_add(engine, "mid",    "(ask + bid) / 2",            &err);
 *   cxpr_formula_add(engine, "signal", "spread / mid > $threshold",  &err);
 *   cxpr_formula_compile(engine, &err);
 *   cxpr_formula_eval_all(engine, ctx, &err);
 *   double signal = cxpr_formula_get(engine, "signal", NULL);  // 0.0 or 1.0
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_formula_engine(void) {
    cxpr_registry*      reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context*        ctx    = cxpr_context_new();
    cxpr_error err = {0};

    /* ask=100.5, bid=99.5 → spread=1.0, mid=100.0, spread/mid=0.01 */
    cxpr_context_set(ctx, "ask", 100.5);
    cxpr_context_set(ctx, "bid",  99.5);
    cxpr_context_set_param(ctx, "threshold", 0.005);  /* 0.5% spread threshold */

    assert(cxpr_formula_add(engine, "spread", "ask - bid",                 &err));
    assert(cxpr_formula_add(engine, "mid",    "(ask + bid) / 2",           &err));
    assert(cxpr_formula_add(engine, "signal", "spread / mid > $threshold", &err));

    assert(cxpr_formula_compile(engine, &err));
    assert(err.code == CXPR_OK);

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_APPROX(cxpr_formula_get(engine, "spread", &found),  1.0);   assert(found);
    ASSERT_APPROX(cxpr_formula_get(engine, "mid",    &found), 100.0);  assert(found);
    ASSERT_APPROX(cxpr_formula_get(engine, "signal", &found),   1.0);  assert(found);

    cxpr_formula_engine_free(engine);
    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_readme_formula_engine\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * README: Domain — Trading signal composition
 *
 *   cxpr_formula_add(engine, "trend",   "close > ema_fast and ema_fast > ema_slow", ...);
 *   cxpr_formula_add(engine, "pullback","close < ema_fast * 0.995", ...);
 *   cxpr_formula_add(engine, "entry",   "trend > 0.5 and pullback > 0.5 and rsi > 50", ...);
 *
 * NOTE: FormulaEngine stores doubles; boolean subformulas must be
 * composed only through their numeric 1.0/0.0 representation.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_readme_domain_trading(void) {
    cxpr_registry*       reg    = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_formula_engine* engine = cxpr_formula_engine_new(reg);
    cxpr_context*        ctx    = cxpr_context_new();
    cxpr_error err = {0};

    /* Example market state from the README formulas */
    cxpr_context_set(ctx, "close",    100.0);
    cxpr_context_set(ctx, "ema_fast", 99.0);
    cxpr_context_set(ctx, "ema_slow", 98.0);
    cxpr_context_set(ctx, "rsi",      55.0);

    /* These three formulas each produce 1.0 (true) or 0.0 (false) */
    assert(cxpr_formula_add(engine, "trend",    "close > ema_fast and ema_fast > ema_slow", &err));
    assert(cxpr_formula_add(engine, "pullback", "close < ema_fast * 0.995", &err));
    assert(cxpr_formula_add(engine, "entry",
                            "trend > 0.5 and pullback > 0.5 and rsi > 50", &err));

    assert(cxpr_formula_compile(engine, &err));
    assert(err.code == CXPR_OK);

    cxpr_formula_eval_all(engine, ctx, &err);
    assert(err.code == CXPR_OK);

    bool found;
    ASSERT_APPROX(cxpr_formula_get(engine, "trend",    &found), 1.0); assert(found);
    ASSERT_APPROX(cxpr_formula_get(engine, "pullback", &found), 0.0); assert(found);
    ASSERT_APPROX(cxpr_formula_get(engine, "entry",    &found), 0.0); assert(found);

    /* No signal when RSI is too low */
    cxpr_context_set(ctx, "rsi", 40.0);
    cxpr_formula_eval_all(engine, ctx, &err);
    ASSERT_APPROX(cxpr_formula_get(engine, "entry", NULL), 0.0);

    cxpr_formula_engine_free(engine);
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
    double cmd_vel = cxpr_ast_eval_double(stop_expr, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(cmd_vel, 2.0);  /* max_speed * 1 */

    /* Obstacle close: distance_front < stop_distance → stop */
    cxpr_context_set(ctx, "distance_front", 0.20);
    cmd_vel = cxpr_ast_eval_double(stop_expr, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    ASSERT_APPROX(cmd_vel, 0.0);

    cxpr_ast* slip_guard = cxpr_parse(parser,
        "slip_ratio > $max_slip", &err);
    assert(slip_guard);
    assert(cxpr_ast_eval_bool(slip_guard, ctx, reg, &err) == false);

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
 *   cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);
 *   // "distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z) < $capture_radius"
 *
 *   — and the struct-param version with define_fn: —
 *
 *   cxpr_registry_define_fn(reg, "dist2(a, b) => sqrt((a.x-b.x)^2 + (a.y-b.y)^2)");
 *   // "dist2(goal, pose) < $capture_radius"
 * ═══════════════════════════════════════════════════════════════════════════ */

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

    cxpr_registry_add(reg, "distance3", fn_distance3, 6, 6, NULL, NULL);
    assert(cxpr_registry_define_fn(reg,
        "dist2(a, b) => sqrt((a.x-b.x)^2 + (a.y-b.y)^2)").code == CXPR_OK);

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
    assert(cxpr_ast_eval_bool(ast3, ctx, reg, &err) == true);   /* dist=4 < 5 */

    /* 2-D version via struct params */
    const char* xy[] = {"x", "y"};
    double a_vals[] = {3.0, 0.0};
    double b_vals[] = {0.0, 4.0};
    cxpr_context_set_fields(ctx, "a", xy, a_vals, 2);
    cxpr_context_set_fields(ctx, "b", xy, b_vals, 2);

    cxpr_ast* ast2 = cxpr_parse(parser, "dist2(a, b) < $capture_radius", &err);
    assert(ast2);
    assert(cxpr_ast_eval_bool(ast2, ctx, reg, &err) == false);  /* dist=5 < 5 is false */

    /* Exact boundary */
    cxpr_context_set_param(ctx, "capture_radius", 5.0);
    bool in_range = cxpr_ast_eval_bool(ast2, ctx, reg, &err);
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
    double _r = cxpr_ast_eval_double(_a, ctx, reg, &err); \
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
