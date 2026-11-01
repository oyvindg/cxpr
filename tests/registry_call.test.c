#include <cxpr/cxpr.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static cxpr_value as_bool(const cxpr_value* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return cxpr_bool(args[0].d > args[1].d);
}

static cxpr_value invert_value(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    assert(args[0].type == CXPR_VALUE_BOOL);
    return cxpr_bool(!args[0].b);
}

static cxpr_value is_1h_value(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    assert(args[0].type == CXPR_VALUE_STRING);
    return cxpr_bool(strcmp(args[0].str, "1h") == 0);
}

static cxpr_value add_int64_exact(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 2);
    assert(args[0].type == CXPR_VALUE_INT64);
    assert(args[1].type == CXPR_VALUE_INT64);
    return cxpr_int64(args[0].i64 + args[1].i64);
}

static void test_registry_call_variants(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_value typed_args[2];
    cxpr_value result;
    cxpr_value_type arg_types[] = {CXPR_VALUE_NUMBER, CXPR_VALUE_NUMBER};
    const cxpr_value_type int64_arg_types[] = {
        CXPR_VALUE_INT64, CXPR_VALUE_INT64
    };

    assert(reg);
    cxpr_registry_add_typed(reg, "gt", as_bool, 2, 2, arg_types, CXPR_VALUE_BOOL, NULL, NULL);

    typed_args[0] = cxpr_num(5.0);
    typed_args[1] = cxpr_num(3.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    cxpr_registry_add_typed(reg, "add_i64", add_int64_exact, 2, 2,
                            int64_arg_types, CXPR_VALUE_INT64, NULL, NULL);
    typed_args[0] = cxpr_int64(INT64_C(9007199254740993));
    typed_args[1] = cxpr_int64(2);
    result = cxpr_registry_call_typed(reg, "add_i64", typed_args, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_INT64);
    assert(result.i64 == INT64_C(9007199254740995));

    {
        cxpr_expr_parser* parser = cxpr_expr_parser_new();
        cxpr_context* context = cxpr_context_new();
        cxpr_expr_ast* ast = cxpr_expr_ast_parse(
            parser, "add_i64(9007199254740993, 2i64)", &err);
        cxpr_expr_compiled* program;
        cxpr_value tree;
        cxpr_value ir;
        assert(ast != NULL);
        program = cxpr_expr_compile(ast, reg, &err);
        assert(program != NULL);
        assert(cxpr_expr_compiled_ir_result_kind(program) == CXPR_IR_RESULT_INT64);
        assert(cxpr_eval_ast(ast, context, reg, &tree, &err));
        assert(tree.type == CXPR_VALUE_INT64 &&
               tree.i64 == INT64_C(9007199254740995));
        assert(cxpr_expr_compiled_eval(program, context, reg, &ir, &err));
        assert(ir.type == CXPR_VALUE_INT64 && ir.i64 == tree.i64);
        cxpr_expr_compiled_free(program);
        cxpr_expr_ast_free(ast);
        cxpr_context_free(context);
        cxpr_expr_parser_free(parser);
    }

    result = cxpr_registry_call_value(reg, "gt", (double[]){1.0, 2.0}, 2, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == false);

    typed_args[0] = cxpr_bool(true);
    typed_args[1] = cxpr_num(1.0);
    result = cxpr_registry_call_typed(reg, "gt", typed_args, 2, &err);
    assert(isnan(result.d));
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_registry_add_value(reg, "invert", invert_value, 1, 1, NULL, NULL);
    typed_args[0] = cxpr_bool(false);
    result = cxpr_registry_call_typed(reg, "invert", typed_args, 1, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    cxpr_registry_add_value(reg, "is_1h", is_1h_value, 1, 1, NULL, NULL);
    typed_args[0] = cxpr_string("1h");
    result = cxpr_registry_call_typed(reg, "is_1h", typed_args, 1, &err);
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL);
    assert(result.b == true);

    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_call_variants();
    printf("  \xE2\x9C\x93 registry_call\n");
    return 0;
}
