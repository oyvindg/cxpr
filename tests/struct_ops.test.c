#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static cxpr_value eval_ast_value(const char* expr,
                                 cxpr_context* ctx,
                                 cxpr_registry* reg,
                                 cxpr_error* err) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_value out;

    *err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(parser, expr, err);
    assert(ast != NULL);
    assert(err->code == CXPR_OK);
    out = cxpr_test_eval_ast(ast, ctx, reg, err);

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return out;
}

static cxpr_value eval_program_value(const char* expr,
                                     cxpr_context* ctx,
                                     cxpr_registry* reg,
                                     cxpr_error* err) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    cxpr_value out = cxpr_num(NAN);

    *err = (cxpr_error){0};
    ast = cxpr_expr_ast_parse(parser, expr, err);
    assert(ast != NULL);
    assert(err->code == CXPR_OK);
    program = cxpr_expr_compile(ast, reg, err);
    assert(program != NULL);
    assert(err->code == CXPR_OK);
    assert(cxpr_expr_compiled_eval(program, ctx, reg, &out, err));

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return out;
}

static void set_vec(cxpr_context* ctx, const char* name, double x, double y) {
    const char* fields[] = {"x", "y"};
    cxpr_value values[] = {cxpr_num(x), cxpr_num(y)};
    cxpr_struct_value* vec = cxpr_struct_value_new(fields, values, 2u);

    assert(vec != NULL);
    cxpr_context_set_struct(ctx, name, vec);
    cxpr_struct_value_free(vec);
}

static void assert_vec(cxpr_value value, double x, double y) {
    assert(value.type == CXPR_VALUE_STRUCT);
    assert(value.s != NULL);
    assert(value.s->field_count == 2u);
    assert(strcmp(value.s->field_names[0], "x") == 0);
    assert(value.s->field_values[0].type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(value.s->field_values[0].d, x);
    assert(strcmp(value.s->field_names[1], "y") == 0);
    assert(value.s->field_values[1].type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(value.s->field_values[1].d, y);
}

static void test_struct_struct_arithmetic_ast_and_ir(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_value ast;
    cxpr_value ir;

    set_vec(ctx, "a", 2.0, 4.0);
    set_vec(ctx, "b", 3.0, 5.0);

    ast = eval_ast_value("a * b", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert_vec(ast, 6.0, 20.0);
    cxpr_value_free(&ast);

    ir = eval_program_value("a * b", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert_vec(ir, 6.0, 20.0);
    cxpr_value_free(&ir);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_struct_struct_arithmetic_ast_and_ir\n");
}

static void test_scalar_struct_arithmetic_preserves_operand_order(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_value doubled;
    cxpr_value shifted;

    set_vec(ctx, "a", 2.0, 4.0);

    doubled = eval_program_value("2 * a", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert_vec(doubled, 4.0, 8.0);
    cxpr_value_free(&doubled);

    shifted = eval_program_value("10 - a", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert_vec(shifted, 8.0, 6.0);
    cxpr_value_free(&shifted);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_scalar_struct_arithmetic_preserves_operand_order\n");
}

static void test_record_literal_struct_arithmetic_compiles(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_value out;

    out = eval_program_value("{ x = 1, y = 2 } * 3", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert_vec(out, 3.0, 6.0);
    cxpr_value_free(&out);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_record_literal_struct_arithmetic_compiles\n");
}

static void test_record_literal_field_mismatch_fails_compile(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, "{ x = 1, y = 2 } + { x = 1, z = 2 }", &err);
    cxpr_expr_compiled* program;

    assert(ast != NULL);
    assert(err.code == CXPR_OK);
    program = cxpr_expr_compile(ast, reg, &err);
    assert(program == NULL);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    assert(strstr(err.message, "matching struct fields") != NULL);

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_registry_free(reg);
    printf("  ✓ test_record_literal_field_mismatch_fails_compile\n");
}

static void test_struct_arithmetic_preserves_temporal_fields(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    const char* fields[] = {"fast", "slow"};
    cxpr_value values[] = {cxpr_duration(10), cxpr_duration(20)};
    cxpr_struct_value* periods = cxpr_struct_value_new(fields, values, 2u);
    cxpr_value out;

    assert(periods != NULL);
    cxpr_context_set_struct(ctx, "periods", periods);
    cxpr_struct_value_free(periods);

    out = eval_program_value("periods * 2", ctx, reg, &err);
    assert(err.code == CXPR_OK);
    assert(out.type == CXPR_VALUE_STRUCT);
    assert(out.s->field_values[0].type == CXPR_VALUE_DURATION);
    assert(out.s->field_values[0].i64 == 20);
    assert(out.s->field_values[1].type == CXPR_VALUE_DURATION);
    assert(out.s->field_values[1].i64 == 40);
    cxpr_value_free(&out);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_struct_arithmetic_preserves_temporal_fields\n");
}

static void test_struct_arithmetic_requires_matching_fields(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_value out;
    const char* fields[] = {"x", "z"};
    cxpr_value values[] = {cxpr_num(3.0), cxpr_num(5.0)};
    cxpr_struct_value* other = cxpr_struct_value_new(fields, values, 2u);

    set_vec(ctx, "a", 2.0, 4.0);
    assert(other != NULL);
    cxpr_context_set_struct(ctx, "other", other);
    cxpr_struct_value_free(other);

    out = eval_ast_value("a + other", ctx, reg, &err);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);
    cxpr_value_free(&out);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    printf("  ✓ test_struct_arithmetic_requires_matching_fields\n");
}

int main(void) {
    printf("Running struct operator tests...\n");
    test_struct_struct_arithmetic_ast_and_ir();
    test_scalar_struct_arithmetic_preserves_operand_order();
    test_record_literal_struct_arithmetic_compiles();
    test_record_literal_field_mismatch_fails_compile();
    test_struct_arithmetic_preserves_temporal_fields();
    test_struct_arithmetic_requires_matching_fields();
    printf("All struct operator tests passed!\n");
    return 0;
}
