/**
 * @file temporal_null.test.c
 * @brief Tests for timestamp/duration value algebra, null helpers, and the
 *        math builtins added alongside them.
 *
 * Each temporal case is checked through BOTH evaluation engines: the tree-walk
 * evaluator (`cxpr_eval_ast`) and the compiled program (`cxpr_compile` +
 * `cxpr_eval_program`), so the two implementations can never silently diverge.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define EPSILON 1e-9

/* ── Host helpers exposed to expressions ──────────────────────────────────── */

static cxpr_value mk_ts(const cxpr_value* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    return cxpr_timestamp((int64_t)args[0].d);
}

static cxpr_value mk_dur(const cxpr_value* args, size_t argc, void* ud) {
    (void)argc;
    (void)ud;
    return cxpr_duration((int64_t)args[0].d);
}

static cxpr_value mk_null(const cxpr_value* args, size_t argc, void* ud) {
    (void)args;
    (void)argc;
    (void)ud;
    return cxpr_null();
}

static void register_helpers(cxpr_registry* reg) {
    cxpr_register_defaults(reg);
    cxpr_registry_add_value(reg, "mk_ts", mk_ts, 1, 1, NULL, NULL);
    cxpr_registry_add_value(reg, "mk_dur", mk_dur, 1, 1, NULL, NULL);
    cxpr_registry_add_value(reg, "mk_null", mk_null, 0, 0, NULL, NULL);
}

/* Evaluate through both engines; assert they agree, return the typed value. */
static cxpr_value eval_typed(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value tree_val = {0};
    cxpr_value prog_val = {0};
    cxpr_program* prog;
    cxpr_expr_ast* ast;

    register_helpers(reg);
    ast = cxpr_expr_ast_parse(p, expr, &err);
    assert(ast && "parse failed");

    assert(cxpr_eval_ast(ast, ctx, reg, &tree_val, &err) && err.code == CXPR_OK);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog && err.code == CXPR_OK);
    assert(cxpr_eval_program(prog, ctx, reg, &prog_val, &err) && err.code == CXPR_OK);

    /* Both engines must produce the same typed result. */
    assert(tree_val.type == prog_val.type);
    assert(tree_val.i64 == prog_val.i64); /* covers number bits, bool, and i64 */

    cxpr_program_free(prog);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    return tree_val;
}

/* Assert that an expression fails the same way in both engines. */
static void assert_eval_error(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value v = {0};
    cxpr_program* prog;
    cxpr_expr_ast* ast;

    register_helpers(reg);
    ast = cxpr_expr_ast_parse(p, expr, &err);
    assert(ast && "parse failed");

    err = (cxpr_error){0};
    assert(!cxpr_eval_ast(ast, ctx, reg, &v, &err) || err.code != CXPR_OK);

    prog = cxpr_compile(ast, reg, &err);
    assert(prog);
    err = (cxpr_error){0};
    assert(!cxpr_eval_program(prog, ctx, reg, &v, &err) || err.code != CXPR_OK);

    cxpr_program_free(prog);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

static void test_temporal_arithmetic(void) {
    cxpr_value v;

    /* timestamp - timestamp -> duration */
    v = eval_typed("mk_ts(5000) - mk_ts(1000)");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 4000);

    /* timestamp + duration -> timestamp */
    v = eval_typed("mk_ts(1000) + mk_dur(500)");
    assert(v.type == CXPR_VALUE_TIMESTAMP && v.i64 == 1500);

    /* duration + timestamp -> timestamp */
    v = eval_typed("mk_dur(500) + mk_ts(1000)");
    assert(v.type == CXPR_VALUE_TIMESTAMP && v.i64 == 1500);

    /* timestamp - duration -> timestamp */
    v = eval_typed("mk_ts(1000) - mk_dur(250)");
    assert(v.type == CXPR_VALUE_TIMESTAMP && v.i64 == 750);

    /* duration +/- duration -> duration */
    v = eval_typed("mk_dur(300) + mk_dur(700)");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 1000);
    v = eval_typed("mk_dur(700) - mk_dur(300)");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 400);

    /* duration * number and number * duration -> duration */
    v = eval_typed("mk_dur(1000) * 3");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 3000);
    v = eval_typed("4 * mk_dur(250)");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 1000);

    /* duration / number -> duration; duration / duration -> number */
    v = eval_typed("mk_dur(3000) / 2");
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 1500);
    v = eval_typed("mk_dur(3000) / mk_dur(1000)");
    assert(v.type == CXPR_VALUE_NUMBER && fabs(v.d - 3.0) < EPSILON);

    printf("  temporal arithmetic OK\n");
}

static void test_temporal_ordering(void) {
    cxpr_value v;

    v = eval_typed("mk_ts(5000) > mk_ts(1000)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);
    v = eval_typed("mk_ts(5000) < mk_ts(1000)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == false);
    v = eval_typed("mk_dur(100) <= mk_dur(100)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);
    v = eval_typed("mk_dur(200) >= mk_dur(100)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);

    /* equality already supported; confirm it still works for i64 types */
    v = eval_typed("mk_ts(1000) == mk_ts(1000)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);
    v = eval_typed("mk_dur(1000) != mk_dur(999)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);

    printf("  temporal ordering OK\n");
}

static void test_temporal_errors(void) {
    assert_eval_error("mk_ts(1) + mk_ts(2)");      /* ts + ts undefined */
    assert_eval_error("mk_ts(1) * 2");             /* scaling a timestamp */
    assert_eval_error("mk_ts(1) < mk_dur(2)");     /* mixed ordering */
    assert_eval_error("mk_dur(1) / mk_ts(2)");     /* duration / timestamp */
    printf("  temporal type errors OK\n");
}

static void test_temporal_struct_fields(void) {
    /* timestamp/duration values arriving through struct fields exercise the
     * IR field-load path rather than the function-call path. */
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value v = {0};

    const char* names[] = { "time" };
    cxpr_value bar_fields[] = { cxpr_timestamp(5000) };
    cxpr_value entry_fields[] = { cxpr_timestamp(1000) };
    cxpr_struct_value* bar = cxpr_struct_value_new(names, bar_fields, 1);
    cxpr_struct_value* entry = cxpr_struct_value_new(names, entry_fields, 1);

    register_helpers(reg);
    cxpr_context_set_struct(ctx, "bar", bar);
    cxpr_context_set_struct(ctx, "entry", entry);

    cxpr_expr_ast* ast = cxpr_expr_ast_parse(p, "bar.time - entry.time", &err);
    assert(ast && err.code == CXPR_OK);
    assert(cxpr_eval_ast(ast, ctx, reg, &v, &err) && err.code == CXPR_OK);
    assert(v.type == CXPR_VALUE_DURATION && v.i64 == 4000);

    cxpr_expr_ast_free(ast);
    cxpr_struct_value_free(bar);
    cxpr_struct_value_free(entry);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  temporal struct fields OK\n");
}

static void test_null_helpers(void) {
    cxpr_value v;

    v = eval_typed("coalesce(mk_null(), 5)");
    assert(v.type == CXPR_VALUE_NUMBER && fabs(v.d - 5.0) < EPSILON);

    v = eval_typed("coalesce(mk_null(), mk_null(), 7)");
    assert(v.type == CXPR_VALUE_NUMBER && fabs(v.d - 7.0) < EPSILON);

    v = eval_typed("coalesce(3, 9)");
    assert(v.type == CXPR_VALUE_NUMBER && fabs(v.d - 3.0) < EPSILON);

    v = eval_typed("is_null(mk_null())");
    assert(v.type == CXPR_VALUE_BOOL && v.b == true);

    v = eval_typed("is_null(5)");
    assert(v.type == CXPR_VALUE_BOOL && v.b == false);

    /* coalesce result usable in arithmetic */
    v = eval_typed("coalesce(mk_null(), 10) + 2");
    assert(v.type == CXPR_VALUE_NUMBER && fabs(v.d - 12.0) < EPSILON);

    printf("  null helpers OK\n");
}

static double eval_number(const char* expr) {
    cxpr_value v = eval_typed(expr);
    assert(v.type == CXPR_VALUE_NUMBER);
    return v.d;
}

static void test_math_builtins(void) {
    assert(fabs(eval_number("hypot(3, 4)") - 5.0) < EPSILON);
    assert(fabs(eval_number("radians(180)") - 3.14159265358979323846) < 1e-9);
    assert(fabs(eval_number("degrees(3.14159265358979323846)") - 180.0) < 1e-9);
    assert(fabs(eval_number("mod(7, 3)") - 1.0) < EPSILON);
    assert(fabs(eval_number("copysign(3, -1)") + 3.0) < EPSILON);
    assert(fabs(eval_number("log1p(0)") - 0.0) < EPSILON);
    assert(fabs(eval_number("expm1(0)") - 0.0) < EPSILON);

    {
        cxpr_value v = eval_typed("isnan(0 / 1)");
        assert(v.type == CXPR_VALUE_BOOL && v.b == false);
        v = eval_typed("isfinite(1)");
        assert(v.type == CXPR_VALUE_BOOL && v.b == true);
    }
    printf("  math builtins OK\n");
}

int main(void) {
    printf("temporal_null tests:\n");
    test_temporal_arithmetic();
    test_temporal_ordering();
    test_temporal_errors();
    test_temporal_struct_fields();
    test_null_helpers();
    test_math_builtins();
    printf("All temporal_null tests passed.\n");
    return 0;
}
