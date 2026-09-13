#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "generated_history_function.h"

static bool evaluate(const char* source, cxpr_context* context,
                     cxpr_registry* registry, bool compiled) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* program = NULL;
    cxpr_error err = {0};
    cxpr_value value = {0};
    assert(parser);
    ast = cxpr_expr_ast_parse(parser, source, &err);
    assert(ast && err.code == CXPR_OK);
    if (compiled) {
        program = cxpr_expr_compile(ast, registry, &err);
        assert(program);
        assert(cxpr_expr_compiled_eval(program, context, registry, &value, &err));
    } else {
        assert(cxpr_eval_ast(ast, context, registry, &value, &err));
    }
    assert(err.code == CXPR_OK && value.type == CXPR_VALUE_BOOL);
    cxpr_expr_compiled_free(program);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return value.b;
}

int main(void) {
    static const double close[] = {10.0, 12.0, 11.0, 15.0, 15.0};
    int64_t cursor = 0;
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* eval_context = cxpr_context_new();
    cxpr_model* model;
    cxpr_model_compiled* program;
    cxpr_model_session* session;
    cxpr_context* context;
    cxpr_error err = {0};
    bool model_value = false;
    assert(registry && eval_context);
    assert(cxpr_register_history_contiguous_numbers(
        registry, "close", close, sizeof(close) / sizeof(close[0]), &cursor,
        CXPR_HISTORY_BOUNDS_ERROR));
    model = cxpr_model_parse(
        "model generated_history_parity\n"
        "in { close }\n"
        "up = close > close[1]\n"
        "out up\n",
        &err);
    assert(model);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);
    session = cxpr_model_session_new(program, NULL, &err);
    assert(session);
    context = cxpr_model_session_context(session);
    assert(context);

    for (cursor = 0; cursor < (int64_t)(sizeof(close) / sizeof(close[0])); ++cursor) {
        const bool generated = generated_history_up(
            close, sizeof(close) / sizeof(close[0]), (size_t)cursor);
        cxpr_context_set(context, "close", close[cursor]);
        cxpr_context_set(eval_context, "close", close[cursor]);
        assert(cxpr_model_session_tick(program, session, NULL, &err));
        assert(cxpr_model_session_get_bool(session, "up", &model_value));
        assert(model_value == generated);
        if (cursor > 0) {
            assert(evaluate("close > close[1]", eval_context, registry, false) == generated);
            assert(evaluate("close > close[1]", eval_context, registry, true) == generated);
        }
    }

    cxpr_model_session_free(session);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_context_free(eval_context);
    cxpr_registry_free(registry);
    puts("history tree/IR/model/generated-C parity OK");
    return 0;
}
