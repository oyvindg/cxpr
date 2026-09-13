#include <cxpr/cxpr.h>

#include "cxpr_test_internal.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>

static uint32_t next_random(uint32_t* state) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

int main(void) {
    cxpr_value elements[8];
    cxpr_value array;
    cxpr_context* context = cxpr_context_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error error = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program;
    uint32_t random = UINT32_C(0x71c0ffee);
    assert(context && registry && parser);
    for (size_t i = 0u; i < 8u; ++i) elements[i] = cxpr_num((double)i * 3.0);
    array = cxpr_array(cxpr_array_value_new(elements, 8u));
    assert(array.a);
    cxpr_context_set_value(context, "values", &array);
    cxpr_value_free(&array);
    ast = cxpr_expr_ast_parse(parser, "values[i]", &error);
    assert(ast && error.code == CXPR_OK);
    program = cxpr_expr_compile(ast, registry, &error);
    assert(program && error.code == CXPR_OK);

    for (size_t trial = 0u; trial < 1000u; ++trial) {
        const uint32_t sample = next_random(&random);
        const bool valid = (sample & 3u) != 0u;
        const double index = valid ? (double)(sample % 8u)
                                   : ((sample & 4u) ? -1.0 : 0.5);
        cxpr_error tree_error = {0};
        cxpr_error ir_error = {0};
        cxpr_value tree = cxpr_null();
        cxpr_value ir;
        cxpr_context_set(context, "i", index);
        if (valid) {
            assert(cxpr_eval_ast(ast, context, registry, &tree, &tree_error));
        } else {
            assert(!cxpr_eval_ast(ast, context, registry, &tree, &tree_error));
        }
        ir = cxpr_test_eval_program(program, context, registry, &ir_error);
        assert(tree_error.code == ir_error.code);
        if (valid) {
            assert(tree.type == CXPR_VALUE_NUMBER && ir.type == CXPR_VALUE_NUMBER);
            assert(tree.d == ir.d && tree.d == index * 3.0);
        } else {
            assert(tree_error.code == CXPR_ERR_INVALID_INDEX);
            assert(ir.type == CXPR_VALUE_NUMBER && isnan(ir.d));
        }
        cxpr_value_free(&tree);
        cxpr_value_free(&ir);
    }

    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_registry_free(registry);
    cxpr_context_free(context);
    return 0;
}
