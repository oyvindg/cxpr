#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static void eval_both(const char* source, cxpr_value* tree, cxpr_value* ir,
                      cxpr_error_code expected_error,
                      cxpr_ir_result_kind expected_kind) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error tree_error = {0};
    cxpr_error ir_error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &tree_error);
    cxpr_expr_compiled* program;

    assert(ast != NULL);
    program = cxpr_expr_compile(ast, registry, &ir_error);
    if (!program) {
        fprintf(stderr, "compile failed for %s: %d %s\n", source,
                (int)ir_error.code, ir_error.message ? ir_error.message : "");
        assert(0);
    }
    if (cxpr_expr_compiled_ir_result_kind(program) != expected_kind) {
        fprintf(stderr, "result kind mismatch for %s: got %d expected %d\n", source,
                (int)cxpr_expr_compiled_ir_result_kind(program), (int)expected_kind);
        assert(0);
    }

    (void)cxpr_eval_ast(ast, context, registry, tree, &tree_error);
    (void)cxpr_expr_compiled_eval(program, context, registry, ir, &ir_error);
    if (tree_error.code != expected_error || ir_error.code != expected_error) {
        fprintf(stderr, "error mismatch for %s: tree=%d ir=%d expected=%d\n", source,
                (int)tree_error.code, (int)ir_error.code, (int)expected_error);
        assert(0);
    }

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

static void test_int64_arithmetic_and_promotion(void) {
    cxpr_value tree = {0};
    cxpr_value ir = {0};

    eval_both("9007199254740993i64 + 2i64", &tree, &ir, CXPR_OK,
              CXPR_IR_RESULT_INT64);
    assert(tree.type == CXPR_VALUE_INT64 && ir.type == CXPR_VALUE_INT64);
    assert(tree.i64 == INT64_C(9007199254740995) && ir.i64 == tree.i64);

    eval_both("5i64 / 2i64", &tree, &ir, CXPR_OK, CXPR_IR_RESULT_NUMBER);
    assert(tree.type == CXPR_VALUE_NUMBER && ir.type == CXPR_VALUE_NUMBER);
    assert(tree.d == 2.5 && ir.d == tree.d);

    eval_both("5i64 + 0.5", &tree, &ir, CXPR_OK, CXPR_IR_RESULT_NUMBER);
    assert(tree.type == CXPR_VALUE_NUMBER && ir.type == CXPR_VALUE_NUMBER);
    assert(tree.d == 5.5 && ir.d == tree.d);
}

static void test_unsuffixed_integer_inference(void) {
    cxpr_value tree = {0};
    cxpr_value ir = {0};

    eval_both("9007199254740993 + 2i64", &tree, &ir, CXPR_OK,
              CXPR_IR_RESULT_INT64);
    assert(tree.type == CXPR_VALUE_INT64 && ir.type == CXPR_VALUE_INT64);
    assert(tree.i64 == INT64_C(9007199254740995) && ir.i64 == tree.i64);

    eval_both("42.0 + 1", &tree, &ir, CXPR_OK, CXPR_IR_RESULT_NUMBER);
    assert(tree.type == CXPR_VALUE_NUMBER && ir.type == CXPR_VALUE_NUMBER);
    assert(tree.d == 43.0 && ir.d == tree.d);
}

static void test_cross_type_comparisons_are_exact(void) {
    cxpr_value tree = {0};
    cxpr_value ir = {0};

    eval_both("5i64 == 5.0", &tree, &ir, CXPR_OK, CXPR_IR_RESULT_BOOL);
    assert(tree.type == CXPR_VALUE_BOOL && tree.b && ir.b);

    eval_both("9007199254740993i64 == 9007199254740992.0", &tree, &ir,
              CXPR_OK, CXPR_IR_RESULT_BOOL);
    assert(tree.type == CXPR_VALUE_BOOL && !tree.b && !ir.b);

    eval_both("9007199254740993i64 > 9007199254740992.0", &tree, &ir,
              CXPR_OK, CXPR_IR_RESULT_BOOL);
    assert(tree.b && ir.b);
}

static void test_overflow_error_parity(void) {
    cxpr_value tree = {0};
    cxpr_value ir = {0};
    eval_both("9223372036854775807i64 + 1i64", &tree, &ir,
              CXPR_ERR_OVERFLOW, CXPR_IR_RESULT_INT64);
}

int main(void) {
    test_int64_arithmetic_and_promotion();
    test_unsuffixed_integer_inference();
    test_cross_type_comparisons_are_exact();
    test_overflow_error_parity();
    puts("  â int64 IR phase A parity");
    return 0;
}
