#include <cxpr/cxpr.h>

#include "cxpr_test_internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

static char* read_fixture(const char* relative_path) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    (void)snprintf(path, sizeof(path), "%s/%s", CXPR_TEST_SOURCE_DIR, relative_path);
    file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) return NULL;
    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    source = (char*)malloc((size_t)size + 1u);
    if (!source || fread(source, 1u, (size_t)size, file) != (size_t)size) {
        free(source);
        fclose(file);
        return NULL;
    }
    source[size] = '\0';
    fclose(file);
    return source;
}

static cxpr_value eval(const char* source, cxpr_context* context, cxpr_error* error) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_expr_ast* ast;
    cxpr_value result = cxpr_null();
    assert(parser && registry);
    cxpr_register_defaults(registry);
    ast = cxpr_expr_ast_parse(parser, source, error);
    assert(ast != NULL);
    if (!cxpr_eval_ast(ast, context, registry, &result, error)) result = cxpr_null();
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return result;
}

static cxpr_value eval_compiled(const char* source, cxpr_context* context,
                                cxpr_error* error) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    cxpr_value result = cxpr_null();
    assert(parser && registry);
    cxpr_register_defaults(registry);
    ast = cxpr_expr_ast_parse(parser, source, error);
    assert(ast != NULL);
    program = cxpr_expr_compile(ast, registry, error);
    assert(program != NULL);
    result = cxpr_test_eval_program(program, context, registry, error);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    return result;
}

static void assert_compiles_to_index(const char* source) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &error);
    cxpr_expr_compiled* program;
    bool saw_index = false;
    bool saw_call_ast = false;
    assert(ast != NULL && error.code == CXPR_OK);
    program = cxpr_expr_compile(ast, registry, &error);
    assert(program != NULL && error.code == CXPR_OK);
    for (size_t i = 0; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        if (instruction.op == CXPR_IR_OP_INDEX) saw_index = true;
        if (instruction.op == CXPR_IR_OP_CALL_AST) saw_call_ast = true;
    }
    assert(saw_index);
    assert(!saw_call_ast);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

static void assert_compiles_without_index(const char* source) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &error);
    cxpr_expr_compiled* program;
    assert(ast != NULL && error.code == CXPR_OK);
    program = cxpr_expr_compile(ast, registry, &error);
    assert(program != NULL && error.code == CXPR_OK);
    for (size_t i = 0; i < cxpr_expr_compiled_ir_count(program); ++i) {
        cxpr_ir_instruction instruction;
        assert(cxpr_expr_compiled_ir_instruction(program, i, &instruction));
        assert(instruction.op != CXPR_IR_OP_INDEX);
        assert(instruction.op != CXPR_IR_OP_BUILD_ARRAY);
        assert(instruction.op != CXPR_IR_OP_CALL_AST);
    }
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

static void test_array_fixture_parses(void) {
    cxpr_error error = {0};
    char* source = read_fixture("fixtures/index/arrays.cxpr");
    cxpr_model* model;
    assert(source != NULL);
    model = cxpr_model_parse(source, &error);
    if (!model) fprintf(stderr, "array fixture: %s\n", error.message);
    assert(model != NULL);
    assert(cxpr_model_output_count(model) == 7u);
    assert(strstr(source, "points[selected_index]") != NULL);
    assert(strstr(source, "matrix[1][0]") != NULL);
    assert(strstr(source, "context_values[selected_index]") != NULL);
    cxpr_model_free(model);
    free(source);
}

static void test_literal_and_nested_array_index(void) {
    cxpr_error error = {0};
    cxpr_value result = eval("[10, 20, 30][1]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 20.0);
    cxpr_value_free(&result);

    result = eval("[[1, 2], [3, 4]][1][0]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 3.0);
    cxpr_value_free(&result);

    result = eval_compiled("[10, 20, 30][1]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 20.0);
    cxpr_value_free(&result);

    result = eval_compiled("[[1, 2], [3, 4]][1][0]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 3.0);
    cxpr_value_free(&result);

    result = eval_compiled("[\"a\", \"b\"][1]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_STRING && strcmp(result.str, "b") == 0);
    cxpr_value_free(&result);

    result = eval_compiled("[true, false][0]", NULL, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_BOOL && result.b);
    cxpr_value_free(&result);

    assert_compiles_without_index("[10, 20, 30][1]");
    assert_compiles_without_index("[[1, 2], [3, 4]][1][0]");
    assert_compiles_to_index("[\"a\", \"b\"][1]");
    assert_compiles_to_index("[10, 20, 30][i]");
    assert_compiles_to_index("[[1, 2], [3, 4]][i][0]");
}

static void test_context_array_and_owned_struct_result(void) {
    static const char* fields[] = {"x", "y"};
    const cxpr_value point_fields[] = {cxpr_num(4.0), cxpr_num(7.0)};
    cxpr_value points[2];
    cxpr_value array;
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_value result;

    points[0] = cxpr_struct(cxpr_struct_value_new(fields, point_fields, 2u));
    points[1] = cxpr_struct(cxpr_struct_value_new(fields, point_fields, 2u));
    array = cxpr_array(cxpr_array_value_new(points, 2u));
    cxpr_context_set_value(context, "points", &array);
    cxpr_context_set(context, "i", 1.0);

    result = eval("points[i]", context, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_STRUCT && result.s != NULL);
    assert(result.s != points[1].s);
    assert(strcmp(result.s->field_names[0], "x") == 0);
    assert(result.s->field_values[0].d == 4.0);

    cxpr_value_free(&result);

    result = eval_compiled("points[i]", context, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_STRUCT && result.s != NULL);
    assert(result.s != points[1].s);
    assert(result.s->field_values[1].d == 7.0);
    cxpr_value_free(&array);
    cxpr_value_free(&points[0]);
    cxpr_value_free(&points[1]);
    cxpr_context_free(context);
    assert(strcmp(result.s->field_names[1], "y") == 0);
    assert(result.s->field_values[1].d == 7.0);
    cxpr_value_free(&result);
}

static void expect_dynamic_index_error(double index, cxpr_error_code expected,
                                       const char* message) {
    cxpr_context* context = cxpr_context_new();
    cxpr_error error = {0};
    cxpr_value result;
    assert(context != NULL);
    cxpr_context_set(context, "i", index);

    result = eval("[1][i]", context, &error);
    assert(result.type == CXPR_VALUE_NULL);
    assert(error.code == expected);
    assert(error.message && strcmp(error.message, message) == 0);
    cxpr_value_free(&result);

    error = (cxpr_error){0};
    result = eval_compiled("[1][i]", context, &error);
    assert(result.type == CXPR_VALUE_NUMBER && isnan(result.d));
    assert(error.code == expected);
    assert(error.message && strcmp(error.message, message) == 0);
    cxpr_value_free(&result);
    cxpr_context_free(context);
}

static cxpr_value next_index(const cxpr_value* args, size_t argc, void* userdata) {
    int* calls = (int*)userdata;
    (void)args;
    assert(argc == 0u);
    ++*calls;
    return cxpr_num(1.0);
}

static void test_dynamic_index_is_evaluated_once(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    cxpr_value result;
    int calls = 0;
    assert(parser && registry);
    cxpr_registry_add_typed(registry, "next_index", next_index, 0u, 0u, NULL,
                            CXPR_VALUE_NUMBER, &calls, NULL);
    ast = cxpr_expr_ast_parse(parser, "[10, 20][next_index()]", &error);
    assert(ast != NULL && error.code == CXPR_OK);

    assert(cxpr_eval_ast(ast, NULL, registry, &result, &error));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 20.0);
    assert(calls == 1);
    cxpr_value_free(&result);

    calls = 0;
    program = cxpr_expr_compile(ast, registry, &error);
    assert(program != NULL && error.code == CXPR_OK);
    result = cxpr_test_eval_program(program, NULL, registry, &error);
    assert(error.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 20.0);
    assert(calls == 1);
    cxpr_value_free(&result);

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

static void expect_index_error(const char* source, cxpr_error_code expected) {
    cxpr_error error = {0};
    cxpr_value result = eval(source, NULL, &error);
    assert(result.type == CXPR_VALUE_NULL);
    assert(error.code == expected);
    cxpr_value_free(&result);
}

static void expect_compiled_index_error(const char* source, cxpr_error_code expected) {
    cxpr_error error = {0};
    cxpr_value result = eval_compiled(source, NULL, &error);
    assert(result.type == CXPR_VALUE_NUMBER && isnan(result.d));
    assert(error.code == expected);
    cxpr_value_free(&result);
}

int main(void) {
    test_array_fixture_parses();
    test_literal_and_nested_array_index();
    test_context_array_and_owned_struct_result();
    test_dynamic_index_is_evaluated_once();
    expect_index_error("[1][-1]", CXPR_ERR_INVALID_INDEX);
    expect_index_error("[1][0.5]", CXPR_ERR_INVALID_INDEX);
    expect_index_error("[][0]", CXPR_ERR_INDEX_OUT_OF_RANGE);
    expect_index_error("[1][1]", CXPR_ERR_INDEX_OUT_OF_RANGE);
    expect_compiled_index_error("[1][-1]", CXPR_ERR_INVALID_INDEX);
    expect_compiled_index_error("[1][0.5]", CXPR_ERR_INVALID_INDEX);
    expect_compiled_index_error("[][0]", CXPR_ERR_INDEX_OUT_OF_RANGE);
    expect_compiled_index_error("[1][1]", CXPR_ERR_INDEX_OUT_OF_RANGE);
    expect_dynamic_index_error(NAN, CXPR_ERR_INVALID_INDEX,
                               "Array index must be a finite non-negative integer");
    expect_dynamic_index_error(INFINITY, CXPR_ERR_INVALID_INDEX,
                               "Array index must be a finite non-negative integer");
    expect_dynamic_index_error(1e30, CXPR_ERR_INVALID_INDEX,
                               "Array index must be a finite non-negative integer");
    puts("cxpr array index tests passed.");
    return 0;
}
