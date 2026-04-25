#include <cxpr/cxpr.h>
#include "../src/limits.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool cxpr_eval_bind_call_args(const cxpr_ast* call_ast,
                              const cxpr_func_entry* entry,
                              const cxpr_ast** out_args,
                              cxpr_error* err);
const char* cxpr_eval_prepare_const_key_for_producer(const cxpr_ast* ast,
                                                     const cxpr_ast* const* ordered_args,
                                                     size_t argc,
                                                     const cxpr_context* ctx,
                                                     const cxpr_registry* reg,
                                                     char* local_buf,
                                                     size_t local_cap,
                                                     char** heap_buf,
                                                     cxpr_error* err);
const char* cxpr_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                        char* local_buf, size_t local_cap, char** heap_buf);

static double sum3(const double* args, size_t argc, void* userdata) {
    (void)argc;
    (void)userdata;
    return args[0] + args[1] + args[2];
}

static int g_struct_call_count = 0;

static void pair_producer(const double* args, size_t argc,
                          cxpr_value* out, size_t field_count,
                          void* userdata) {
    (void)userdata;
    (void)field_count;
    assert(argc == 2u);
    g_struct_call_count += 1;
    out[0] = cxpr_fv_double(args[0] * 0.1);
    out[1] = cxpr_fv_double(args[1] * 0.5);
}

static void test_eval_call_paths(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    double out = 0.0;
    const char* params[] = {"a", "b", "c"};

    assert(p && ctx && reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add(reg, "sum3", sum3, 3, 3, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "sum3", params, 3));
    assert(cxpr_registry_define_fn(reg, "twice(x) => x * 2").code == CXPR_OK);

    ast = cxpr_parse(p, "sum3(c=3, a=1, b=2)", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 6.0);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "twice(5)", &err);
    assert(ast);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(out == 10.0);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

static void test_named_param_producer_cache_paths(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_program* prog;
    cxpr_value result;
    const char* fields[] = {"line", "histogram"};
    const char* params[] = {"period", "signal"};

    assert(p && ctx && reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add_struct(reg, "macd", pair_producer, 2, 2, fields, 2, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", params, 2));
    cxpr_context_set_param(ctx, "period", 14.0);
    cxpr_context_set_param(ctx, "signal", 3.0);

    g_struct_call_count = 0;
    ast = cxpr_parse(p,
                     "macd(period=$period, signal=$signal).line + "
                     "macd(period=$period, signal=$signal).histogram",
                     &err);
    assert(ast);
    assert(cxpr_eval_ast(ast, ctx, reg, &result, &err));
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER);
    assert(fabs(result.d - 2.9) < 1e-12);
    assert(g_struct_call_count == 1);
    cxpr_ast_free(ast);

    cxpr_context_clear_cached_structs(ctx);
    g_struct_call_count = 0;
    ast = cxpr_parse(p,
                     "macd(period=$period, signal=$signal).line + "
                     "macd(period=$period, signal=$signal).histogram",
                     &err);
    assert(ast);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL && err.code == CXPR_OK);
    {
        const cxpr_ir_program* ir = (const cxpr_ir_program*)prog;
        size_t call_ast_count = 0u;
        size_t call_producer_count = 0u;
        for (size_t i = 0u; i < ir->count; ++i) {
            if (ir->code[i].op == CXPR_OP_CALL_AST) ++call_ast_count;
            if (ir->code[i].op == CXPR_OP_CALL_PRODUCER ||
                ir->code[i].op == CXPR_OP_CALL_PRODUCER_CONST_FIELD) {
                ++call_producer_count;
            }
        }
        assert(call_ast_count == 0u);
        assert(call_producer_count >= 2u);
    }
    assert(cxpr_eval_program(prog, ctx, reg, &result, &err));
    assert(err.code == CXPR_OK);
    assert(result.type == CXPR_VALUE_NUMBER);
    assert(fabs(result.d - 2.9) < 1e-12);
    assert(g_struct_call_count == 1);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

static void test_prepare_const_key_with_param_args(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    cxpr_func_entry* entry;
    const cxpr_ast* ordered_args[CXPR_MAX_CALL_ARGS] = {0};
    char local_buf[256];
    char expected_buf[256];
    char* heap_buf = NULL;
    const char* key;
    const char* expected;
    const char* fields[] = {"line", "histogram"};
    const char* params[] = {"period", "signal"};
    const double expected_values[] = {14.0, 3.0};

    assert(p && ctx && reg);
    cxpr_register_defaults(reg);
    cxpr_registry_add_struct(reg, "macd", pair_producer, 2, 2, fields, 2, NULL, NULL);
    assert(cxpr_registry_set_param_names(reg, "macd", params, 2));
    cxpr_context_set_param(ctx, "period", 14.0);
    cxpr_context_set_param(ctx, "signal", 3.0);

    ast = cxpr_parse(p,
                     "macd(period=$period + 0, signal=$signal * 1).line",
                     &err);
    assert(ast != NULL);
    entry = cxpr_registry_find(reg, "macd");
    assert(entry != NULL);
    assert(cxpr_eval_bind_call_args(ast, entry, ordered_args, &err));

    key = cxpr_eval_prepare_const_key_for_producer(
        ast,
        ordered_args,
        2u,
        ctx,
        reg,
        local_buf,
        sizeof(local_buf),
        &heap_buf,
        &err);
    assert(err.code == CXPR_OK);
    assert(key != NULL);
    expected = cxpr_build_struct_cache_key(
        "macd",
        expected_values,
        2u,
        expected_buf,
        sizeof(expected_buf),
        NULL);
    assert(expected != NULL);
    assert(strcmp(key, expected) == 0);

    free(heap_buf);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
}

int main(void) {
    test_eval_call_paths();
    test_named_param_producer_cache_paths();
    test_prepare_const_key_with_param_args();
    printf("  \xE2\x9C\x93 eval_calls\n");
    return 0;
}
