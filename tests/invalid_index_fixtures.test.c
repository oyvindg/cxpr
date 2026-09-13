#include <cxpr/cxpr.h>

#include "cxpr_test_internal.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

typedef struct {
    const char* fixture;
    cxpr_error_code code;
    const char* message;
    double dynamic_index;
    bool uses_dynamic_index;
    bool uses_history;
    bool test_ir;
} invalid_case;

static char* read_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    (void)snprintf(path, sizeof(path), "%s/fixtures/index/invalid/%s.cxpr",
                   CXPR_TEST_SOURCE_DIR, name);
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

static void assert_backend_error(const invalid_case* test, bool compiled) {
    static const double history_values[] = {10.0};
    int64_t cursor = 0;
    char* source = read_fixture(test->fixture);
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_expr_ast* ast;
    cxpr_error error = {0};
    cxpr_value result = cxpr_null();

    assert(source && parser && registry && context);
    if (test->uses_dynamic_index) {
        cxpr_context_set(context, "invalid_index", test->dynamic_index);
    }
    if (strcmp(test->fixture, "non_indexable") == 0) {
        cxpr_context_set(context, "scalar", 42.0);
    }
    if (test->uses_history) {
        assert(cxpr_register_history_contiguous_numbers(
            registry, "source", history_values, 1u, &cursor,
            CXPR_HISTORY_BOUNDS_ERROR));
    }
    ast = cxpr_expr_ast_parse(parser, source, &error);
    assert(ast != NULL && error.code == CXPR_OK);
    if (compiled) {
        cxpr_expr_compiled* program = cxpr_expr_compile(ast, registry, &error);
        assert(program != NULL && error.code == CXPR_OK);
        result = cxpr_test_eval_program(program, context, registry, &error);
        cxpr_expr_compiled_free(program);
    } else if (!cxpr_eval_ast(ast, context, registry, &result, &error)) {
        result = cxpr_null();
    }
    if (error.code != test->code) {
        fprintf(stderr, "%s (%s): expected error %d, got %d: %s\n",
                test->fixture, compiled ? "IR" : "tree", (int)test->code,
                (int)error.code, error.message ? error.message : "(null)");
    }
    assert(error.code == test->code);
    assert(error.message != NULL && strcmp(error.message, test->message) == 0);

    cxpr_value_free(&result);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
    free(source);
}

int main(void) {
    const invalid_case cases[] = {
        {"negative", CXPR_ERR_INVALID_INDEX,
         "Array index must be a finite non-negative integer", 0.0, false, false, true},
        {"fractional", CXPR_ERR_INVALID_INDEX,
         "Array index must be a finite non-negative integer", 0.0, false, false, true},
        {"nonfinite", CXPR_ERR_INVALID_INDEX,
         "Array index must be a finite non-negative integer", NAN, true, false, true},
        {"overflow", CXPR_ERR_INVALID_INDEX,
         "Array index must be a finite non-negative integer", 1e30, true, false, true},
        {"empty", CXPR_ERR_INDEX_OUT_OF_RANGE,
         "Array index is out of range", 0.0, false, false, true},
        {"out_of_range", CXPR_ERR_INDEX_OUT_OF_RANGE,
         "Array index is out of range", 0.0, false, false, true},
        {"non_indexable", CXPR_ERR_TYPE_MISMATCH,
         "Index target is not indexable", 0.0, true, false, true},
        {"future_history", CXPR_ERR_INVALID_INDEX,
         "Index must be a finite non-negative integer", 0.0, false, true, true},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        assert_backend_error(&cases[i], false);
        if (cases[i].test_ir) assert_backend_error(&cases[i], true);
    }
    puts("cxpr invalid index fixture tests passed.");
    return 0;
}
