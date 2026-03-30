/**
 * Tests for Phase 3: struct-producing functions.
 * Zero-argument producers (triggered by field access) and argument-bearing
 * producers (called via name(args).field syntax).
 *
 * All tests are expected to FAIL TO COMPILE until the Phase 3 implementation
 * is complete.
 */
#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

/* ── helpers ─────────────────────────────────────────────────────────── */

static cxpr_field_value eval_typed(const char *expr,
                                   cxpr_context *ctx, cxpr_registry *reg) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_field_value eval_typed_fails(const char *expr,
                                         cxpr_context *ctx, cxpr_registry *reg,
                                         cxpr_error_code expected) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ast_eval(ast, ctx, reg, &err);
    assert(err.code == expected);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return result;
}

static cxpr_field_value ir_eval_typed(const char *expr,
                                      cxpr_context *ctx, cxpr_registry *reg) {
    cxpr_parser *p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast *ast = cxpr_parse(p, expr, &err);
    assert(ast != NULL && err.code == CXPR_OK);
    cxpr_program *prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL && err.code == CXPR_OK);
    cxpr_field_value result = cxpr_ir_eval(prog, ctx, reg, &err);
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_program_free(prog);
    cxpr_parser_free(p);
    return result;
}

/* ── zero-argument producer ──────────────────────────────────────────── */

static int g_zero_call_count = 0;

static void zero_arg_producer(const double *args, size_t argc,
                               cxpr_field_value *out, size_t field_count,
                               void *userdata) {
    (void)args; (void)argc; (void)userdata; (void)field_count;
    g_zero_call_count++;
    out[0] = cxpr_fv_double(10.0);  /* line */
    out[1] = cxpr_fv_double(3.0);   /* histogram */
}

static void test_zero_arg_producer_basic(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line", "histogram"};
    cxpr_registry_add_struct_producer(reg, "macd", zero_arg_producer,
                                      0, 0, fields, 2, NULL, NULL);

    g_zero_call_count = 0;
    cxpr_field_value r = eval_typed("macd.line", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 10.0);
    assert(g_zero_call_count == 1);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_zero_arg_producer_basic\n");
}

static void test_zero_arg_producer_called_once(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line", "histogram"};
    cxpr_registry_add_struct_producer(reg, "macd", zero_arg_producer,
                                      0, 0, fields, 2, NULL, NULL);

    /* two field accesses on the same producer within one eval → one call */
    g_zero_call_count = 0;
    cxpr_field_value r = eval_typed("macd.line > 0.0 && macd.histogram > 0.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);
    assert(g_zero_call_count == 1);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_zero_arg_producer_called_once\n");
}

/* ── argument-bearing producer ───────────────────────────────────────── */

static int    g_arg_call_count = 0;
static double g_last_period    = 0.0;
static double g_last_signal    = 0.0;

static void arg_producer(const double *args, size_t argc,
                          cxpr_field_value *out, size_t field_count,
                          void *userdata) {
    (void)userdata; (void)field_count;
    assert(argc == 2);
    g_arg_call_count++;
    g_last_period = args[0];
    g_last_signal = args[1];
    out[0] = cxpr_fv_double(args[0] * 0.1);  /* line */
    out[1] = cxpr_fv_double(args[1] * 0.5);  /* histogram */
}

static void test_arg_producer(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line", "histogram"};
    cxpr_registry_add_struct_producer(reg, "macd", arg_producer,
                                      2, 2, fields, 2, NULL, NULL);

    g_arg_call_count = 0;
    cxpr_field_value r = eval_typed("macd(14.0, 3.0).line > 0.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);
    assert(g_arg_call_count == 1);
    ASSERT_DOUBLE_EQ(g_last_period, 14.0);
    ASSERT_DOUBLE_EQ(g_last_signal, 3.0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_arg_producer\n");
}

static void test_arg_producer_called_once_for_two_fields(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line", "histogram"};
    cxpr_registry_add_struct_producer(reg, "macd", arg_producer,
                                      2, 2, fields, 2, NULL, NULL);

    g_arg_call_count = 0;
    eval_typed("macd(14.0, 3.0).line + macd(14.0, 3.0).histogram", ctx, reg);
    assert(g_arg_call_count == 1);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_arg_producer_called_once_for_two_fields\n");
}

/* ── bool output field stays bool ────────────────────────────────────── */

static void bool_output_producer(const double *args, size_t argc,
                                  cxpr_field_value *out, size_t field_count,
                                  void *userdata) {
    (void)args; (void)argc; (void)userdata; (void)field_count;
    out[0] = cxpr_fv_double(5.0);
    out[1] = cxpr_fv_bool(true);
}

static void test_bool_output_field(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"value", "active"};
    cxpr_registry_add_struct_producer(reg, "sensor", bool_output_producer,
                                      0, 0, fields, 2, NULL, NULL);

    cxpr_field_value r = eval_typed("sensor.active", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    /* can be used in logical expression */
    r = eval_typed("sensor.active && sensor.value > 0.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_bool_output_field\n");
}

/* ── host-set struct takes priority over producer ────────────────────── */

static int g_priority_call_count = 0;

static void priority_producer(const double *args, size_t argc,
                               cxpr_field_value *out, size_t field_count,
                               void *userdata) {
    (void)args; (void)argc; (void)userdata; (void)field_count;
    g_priority_call_count++;
    out[0] = cxpr_fv_double(999.0);
}

static void test_host_set_takes_priority(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"v"};
    cxpr_registry_add_struct_producer(reg, "obj", priority_producer,
                                      0, 0, fields, 1, NULL, NULL);

    cxpr_field_value vals[] = {cxpr_fv_double(42.0)};
    cxpr_struct_value *s = cxpr_struct_value_new(fields, vals, 1);
    cxpr_context_set_struct(ctx, "obj", s);
    cxpr_struct_value_free(s);

    g_priority_call_count = 0;
    cxpr_field_value r = eval_typed("obj.v", ctx, reg);
    assert(r.type == CXPR_FIELD_DOUBLE);
    ASSERT_DOUBLE_EQ(r.d, 42.0);
    assert(g_priority_call_count == 0);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_host_set_takes_priority\n");
}

/* ── context_clear removes cached output; next eval re-calls producer ── */

static int g_recall_count = 0;

static void recallable_producer(const double *args, size_t argc,
                                 cxpr_field_value *out, size_t field_count,
                                 void *userdata) {
    (void)args; (void)argc; (void)userdata; (void)field_count;
    g_recall_count++;
    out[0] = cxpr_fv_double((double)g_recall_count);
}

static void test_clear_causes_recall(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"v"};
    cxpr_registry_add_struct_producer(reg, "obj", recallable_producer,
                                      0, 0, fields, 1, NULL, NULL);

    g_recall_count = 0;
    eval_typed("obj.v", ctx, reg);
    assert(g_recall_count == 1);

    cxpr_context_clear(ctx);

    eval_typed("obj.v", ctx, reg);
    assert(g_recall_count == 2);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_clear_causes_recall\n");
}

/* ── wrong arity gives CXPR_ERR_WRONG_ARITY ─────────────────────────── */

static void test_wrong_arity(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"v"};
    cxpr_registry_add_struct_producer(reg, "obj", arg_producer,
                                      2, 2, fields, 1, NULL, NULL);

    eval_typed_fails("obj(1.0).v", ctx, reg, CXPR_ERR_WRONG_ARITY);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_wrong_arity\n");
}

/* ── unknown field on known producer gives UNKNOWN_IDENTIFIER ─────────── */

static void test_unknown_field(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line"};
    cxpr_registry_add_struct_producer(reg, "macd", zero_arg_producer,
                                      0, 0, fields, 1, NULL, NULL);

    eval_typed_fails("macd.nonexistent", ctx, reg, CXPR_ERR_UNKNOWN_IDENTIFIER);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_unknown_field\n");
}

/* ── IR parity ───────────────────────────────────────────────────────── */

static void test_ir_parity(void) {
    cxpr_registry *reg = cxpr_registry_new();
    cxpr_register_builtins(reg);
    cxpr_context *ctx = cxpr_context_new();

    const char *fields[] = {"line", "histogram"};
    cxpr_registry_add_struct_producer(reg, "macd", zero_arg_producer,
                                      0, 0, fields, 2, NULL, NULL);

    /* zero-arg producer */
    g_zero_call_count = 0;
    cxpr_field_value r = ir_eval_typed("macd.line > 0.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);

    cxpr_context_free(ctx);
    ctx = cxpr_context_new();
    cxpr_registry_free(reg);
    reg = cxpr_registry_new();
    cxpr_register_builtins(reg);

    /* arg-bearing producer */
    cxpr_registry_add_struct_producer(reg, "macd", arg_producer,
                                      2, 2, fields, 2, NULL, NULL);
    g_arg_call_count = 0;
    r = ir_eval_typed("macd(14.0, 3.0).line > 0.0", ctx, reg);
    assert(r.type == CXPR_FIELD_BOOL);
    assert(r.b == true);
    assert(g_arg_call_count == 1);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  \u2713 test_ir_parity\n");
}

int main(void) {
    printf("Running struct_producer tests...\n");
    test_zero_arg_producer_basic();
    test_zero_arg_producer_called_once();
    test_arg_producer();
    test_arg_producer_called_once_for_two_fields();
    test_bool_output_field();
    test_host_set_takes_priority();
    test_clear_causes_recall();
    test_wrong_arity();
    test_unknown_field();
    test_ir_parity();
    printf("All struct_producer tests passed!\n");
    return 0;
}
