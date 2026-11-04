#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_eval_tree_access_paths(void) {
    cxpr_expr_parser* p = cxpr_expr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    double out = 0.0;
    bool bout = false;
    const char* fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};

    assert(p && ctx && reg);
    cxpr_context_set_fields(ctx, "pose", fields, values, 2);
    cxpr_context_set(ctx, "legacy.value", 7.0);
    cxpr_context_set_param(ctx, "base.period", 8.0);
    cxpr_context_set_param(ctx, "period", 5.0);

    ast = cxpr_expr_ast_parse(p, "pose.x + pose.y", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 7.0);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "legacy.value == 7", &err);
    assert(ast);
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &bout, &err));
    assert(bout);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "$base.period + $period", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 13.0);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p, "sum(map([1, 2, 3], x => x * 2))", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 12.0);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p,
        "sum([1, 2, 3], x => { doubled = x * 2; shifted = doubled + 1; return shifted; })",
        &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 15.0);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p,
        "reduce(map([1, 2, 3], x => x * 2), 0, step => step.acc + step.value + step.index)",
        &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 15.0);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p,
        "reduce([{ signal = 2, gain = 0.25 }, { signal = 4, gain = 0.5 }], "
        "0, step => step.acc + step.value.signal * step.value.gain)", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 2.5);
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p,
        "sum([{ dry = { left = 1, right = 2 }, wet = { room = { left = 3, right = 4 } } }, "
        "     { dry = { left = 5, right = 6 }, wet = { room = { left = 7, right = 8 } } }])",
        &err);
    assert(ast);
    {
        cxpr_value value = {0};
        assert(cxpr_eval_ast(ast, ctx, reg, &value, &err));
        assert(value.type == CXPR_VALUE_STRUCT && value.s);
        assert(value.s->field_values[0].s->field_values[0].d == 6.0);
        assert(value.s->field_values[0].s->field_values[1].d == 8.0);
        assert(value.s->field_values[1].s->field_values[0].s->field_values[0].d == 10.0);
        assert(value.s->field_values[1].s->field_values[0].s->field_values[1].d == 12.0);
        cxpr_value_free(&value);
    }
    cxpr_expr_ast_free(ast);

    ast = cxpr_expr_ast_parse(p,
        "sum([{ sample = 1, metadata = { name = \"lead\" } }, "
        "     { sample = 2, metadata = { name = \"bass\" } }])", &err);
    assert(ast);
    {
        cxpr_value value = {0};
        assert(!cxpr_eval_ast(ast, ctx, reg, &value, &err));
        assert(err.code == CXPR_ERR_TYPE_MISMATCH);
        assert(err.message && strstr(err.message, "metadata.name"));
    }
    cxpr_expr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_expr_parser_free(p);
}

int main(void) {
    test_eval_tree_access_paths();
    printf("  \xE2\x9C\x93 eval_tree\n");
    return 0;
}
