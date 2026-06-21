#include <cxpr/cxpr.h>
#include <cxpr/expression.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool cxpr_evaluator_reserve_for_entry(cxpr_evaluator* evaluator);

static double combo_scalar(const double* args, size_t argc, void* ud) {
    (void)ud;
    assert(argc == 3);
    return args[0] + args[1] + args[2];
}

static double counted_scalar(const double* args, size_t argc, void* ud) {
    int* calls = (int*)ud;

    assert(argc == 1);
    if (calls) *calls += 1;
    return args[0] * 2.0;
}

static void macd_like_producer(const double* args,
                               size_t argc,
                               cxpr_value* out,
                               size_t field_count,
                               void* ud) {
    int* calls = (int*)ud;

    assert(argc == 3);
    assert(field_count == 3);
    if (calls) *calls += 1;
    out[0] = cxpr_num(args[0] - args[1]);
    out[1] = cxpr_num(args[2]);
    out[2] = cxpr_num((args[0] - args[1]) - args[2]);
}

static void test_expression_evaluator_lifecycle(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    const char* order[4];

    assert(reg && evaluator && ctx);
    cxpr_context_set(ctx, "close", 10.0);
    assert(cxpr_evaluator_reserve_for_entry(evaluator));
    assert(cxpr_expression_add(evaluator, "base", "close + 1", &err));
    assert(cxpr_expression_add(evaluator, "flag", "base > 10", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_double(evaluator, "base", &found) == 11.0 && found);
    assert(cxpr_expression_get_bool(evaluator, "flag", &found) == true && found);
    assert(cxpr_expression_eval_order(evaluator, order, 4) == 2);
    assert(strcmp(order[0], "base") == 0);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_expression_struct_alias_field_prefix(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    const char* fields[] = {"line", "signal", "histogram"};
    const char* params[] = {"source", "fast", "slow", "signal"};

    assert(reg && evaluator && ctx);
    cxpr_context_set(ctx, "close", 10.0);
    cxpr_registry_add(reg, "macd", combo_scalar, 3, 3, NULL, NULL);
    cxpr_registry_add_struct(reg, "macd", macd_like_producer, 3, 3, fields, 3, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", params, 4));

    assert(cxpr_expression_add(evaluator, "m", "macd(fast=12, slow=26, signal=9)", &err));
    assert(cxpr_expression_add(evaluator, "entry", "m.line < m.signal", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(cxpr_expression_get_bool(evaluator, "entry", &found) == true && found);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_expression_evaluator_reuses_scalar_memo_across_batch(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    int calls = 0;

    assert(reg && evaluator && ctx);
    cxpr_context_set(ctx, "close", 10.0);
    cxpr_registry_add(reg, "counted", counted_scalar, 1, 1, &calls, NULL);

    assert(cxpr_expression_add(evaluator, "a", "counted(close) + counted(close)", &err));
    assert(cxpr_expression_add(evaluator, "b", "counted(close) + 1", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(calls == 1);
    assert(cxpr_expression_get_double(evaluator, "a", &found) == 40.0 && found);
    assert(cxpr_expression_get_double(evaluator, "b", &found) == 21.0 && found);

    cxpr_context_set(ctx, "close", 11.0);
    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(calls == 2);
    assert(cxpr_expression_get_double(evaluator, "a", &found) == 44.0 && found);
    assert(cxpr_expression_get_double(evaluator, "b", &found) == 23.0 && found);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_expression_evaluator_reuses_struct_producer_across_batch(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    bool found = false;
    int calls = 0;
    const char* fields[] = {"line", "signal", "histogram"};

    assert(reg && evaluator && ctx);
    cxpr_registry_add_struct(reg, "macd", macd_like_producer, 3, 3, fields, 3, &calls, NULL);

    assert(cxpr_expression_add(evaluator, "sig", "macd(12, 26, 9).signal", &err));
    assert(cxpr_expression_add(evaluator, "hist", "macd(12, 26, 9).histogram", &err));
    assert(cxpr_evaluator_compile(evaluator, &err));

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(calls == 1);
    assert(cxpr_expression_get_double(evaluator, "sig", &found) == 9.0 && found);
    assert(cxpr_expression_get_double(evaluator, "hist", &found) == -23.0 && found);

    cxpr_evaluator_eval(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    assert(calls == 2);

    cxpr_context_free(ctx);
    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

static void test_expression_evaluator_grows_capacity(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};

    assert(reg && evaluator);
    for (size_t i = 0; i < 40u; ++i) {
        char name[32];
        char expr[32];
        snprintf(name, sizeof(name), "e%zu", i);
        snprintf(expr, sizeof(expr), "%zu", i);
        assert(cxpr_expression_add(evaluator, name, expr, &err));
    }

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
}

int main(void) {
    test_expression_evaluator_lifecycle();
    test_expression_struct_alias_field_prefix();
    test_expression_evaluator_reuses_scalar_memo_across_batch();
    test_expression_evaluator_reuses_struct_producer_across_batch();
    test_expression_evaluator_grows_capacity();
    printf("  \xE2\x9C\x93 expression_evaluator\n");
    return 0;
}
