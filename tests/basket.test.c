/**
 * @file basket.test.c
 * @brief Unit tests for cxpr basket helpers.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cxpr_struct_value* make_role_binding(const double* values, size_t value_count) {
    const size_t field_count = value_count + 2;
    const char** names;
    cxpr_value* field_values;
    cxpr_struct_value* out;
    size_t i;

    names = (const char**)calloc(field_count, sizeof(char*));
    field_values = (cxpr_value*)calloc(field_count, sizeof(cxpr_value));
    if (!names || !field_values) {
        free(names);
        free(field_values);
        return NULL;
    }

    names[0] = "bound_count";
    field_values[0] = cxpr_fv_double((double)value_count);
    names[1] = "value_count";
    field_values[1] = cxpr_fv_double((double)value_count);

    for (i = 0; i < value_count; ++i) {
        const int name_len = snprintf(NULL, 0, "v%zu", i);
        char* name;
        if (name_len < 0) {
            free(names);
            free(field_values);
            return NULL;
        }
        name = (char*)malloc((size_t)name_len + 1u);
        if (!name) {
            free(names);
            free(field_values);
            return NULL;
        }
        snprintf(name, (size_t)name_len + 1u, "v%zu", i);
        names[i + 2] = name;
        field_values[i + 2] = cxpr_fv_double(values[i]);
    }

    out = cxpr_struct_value_new(names, field_values, field_count);
    for (i = 2; i < field_count; ++i) {
        free((void*)names[i]);
    }
    free(names);
    free(field_values);
    return out;
}

static cxpr_ast* parse_expr(const char* expr, cxpr_error* err) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_ast* ast = cxpr_parse(p, expr, err);
    cxpr_parser_free(p);
    return ast;
}

static double eval_number(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    cxpr_ast* ast = parse_expr(expr, &err);
    double out = 0.0;
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    return out;
}

static bool eval_bool(const char* expr, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    cxpr_ast* ast = parse_expr(expr, &err);
    bool out = false;
    assert(ast);
    assert(cxpr_eval_ast_bool(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    cxpr_ast_free(ast);
    return out;
}

int main(void) {
    const double values[] = {1.0, 2.0, 3.0};
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_struct_value* role = make_role_binding(values, 3);
    cxpr_error err = {0};
    cxpr_ast* ast;

    assert(ctx);
    assert(reg);
    assert(role);

    cxpr_register_basket_builtins(reg);
    cxpr_context_set_struct(ctx, "__dynasty_role_pair", role);
    cxpr_struct_value_free(role);

    assert(cxpr_basket_is_builtin("avg"));
    assert(cxpr_basket_is_aggregate_function("avg", 1));
    assert(!cxpr_basket_is_aggregate_function("count", 1));

    assert(fabs(eval_number("avg($pair)", ctx, reg) - 2.0) < 1e-10);
    assert(fabs(eval_number("count($pair)", ctx, reg) - 3.0) < 1e-10);
    assert(fabs(eval_number("min($pair)", ctx, reg) - 1.0) < 1e-10);
    assert(fabs(eval_number("max($pair)", ctx, reg) - 3.0) < 1e-10);
    assert(eval_bool("any($pair > 2)", ctx, reg));
    assert(eval_bool("all($pair > 0)", ctx, reg));

    ast = parse_expr("avg($pair)", &err);
    assert(ast);
    assert(cxpr_ast_uses_basket_aggregates(ast));
    cxpr_ast_free(ast);

    assert(cxpr_expression_uses_basket_aggregates("count($pair)"));
    assert(!cxpr_expression_uses_basket_aggregates("rsi > 30"));

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    printf("  \xE2\x9C\x93 test_basket\n");
    return 0;
}
