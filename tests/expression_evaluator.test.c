#include <cxpr/cxpr.h>
#include <cxpr/expression.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool cxpr_evaluator_reserve_for_entry(cxpr_evaluator* evaluator);

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

int main(void) {
    test_expression_evaluator_lifecycle();
    printf("  \xE2\x9C\x93 expression_evaluator\n");
    return 0;
}
