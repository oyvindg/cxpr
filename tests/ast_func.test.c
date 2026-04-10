/**
 * @file ast_func.test.c
 * @brief Unit tests for AST-aware registry functions (cxpr_registry_add_ast).
 */

#include <cxpr/cxpr.h>
#include "cxpr_test_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPSILON 1e-10
#define ASSERT_DOUBLE_EQ(a, b) assert(fabs((a) - (b)) < EPSILON)

static cxpr_ast* parse_or_die(cxpr_parser* p, const char* expr) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n", expr, err.message ? err.message : "(null)");
        assert(0);
    }
    return ast;
}

static cxpr_value pick_param_ast_fn(const cxpr_ast* call_ast,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    void* userdata,
                                    cxpr_error* err) {
    (void)reg;
    (void)userdata;

    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "pick_param") == 0);
    assert(cxpr_ast_function_argc(call_ast) == 1);

    const cxpr_ast* arg = cxpr_ast_function_arg(call_ast, 0);
    if (!arg || cxpr_ast_type(arg) != CXPR_NODE_VARIABLE) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "pick_param expects one $variable argument";
        }
        return cxpr_fv_double(NAN);
    }

    bool found = false;
    const char* param = cxpr_ast_variable_name(arg);
    const double value = cxpr_context_get_param(ctx, param, &found);
    if (!found) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unknown parameter";
        }
        return cxpr_fv_double(NAN);
    }
    return cxpr_fv_double(value);
}

static cxpr_value double_eval_ast_fn(const cxpr_ast* call_ast,
                                     const cxpr_context* ctx,
                                     const cxpr_registry* reg,
                                     void* userdata,
                                     cxpr_error* err) {
    (void)userdata;

    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "double_eval") == 0);
    assert(cxpr_ast_function_argc(call_ast) == 1);

    double value = 0.0;
    if (!cxpr_eval_ast_number(cxpr_ast_function_arg(call_ast, 0), ctx, reg, &value, err)) {
        return cxpr_fv_double(NAN);
    }
    return cxpr_fv_double(value * 2.0);
}

static int g_ast_userdata_free_count = 0;

static void ast_userdata_free(void* userdata) {
    ++g_ast_userdata_free_count;
    free(userdata);
}

static cxpr_value scale_eval_ast_fn(const cxpr_ast* call_ast,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    void* userdata,
                                    cxpr_error* err) {
    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "scale_eval") == 0);
    assert(cxpr_ast_function_argc(call_ast) == 1);

    const int factor = userdata ? *(const int*)userdata : 1;
    double value = 0.0;
    if (!cxpr_eval_ast_number(cxpr_ast_function_arg(call_ast, 0), ctx, reg, &value, err)) {
        return cxpr_fv_double(NAN);
    }
    return cxpr_fv_double(value * (double)factor);
}

static void test_ast_function_can_read_variable_argument(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    cxpr_register_builtins(reg);
    cxpr_registry_add_ast(reg, "pick_param", pick_param_ast_fn, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);

    cxpr_context_set_param(ctx, "threshold", 5.5);
    cxpr_ast* ast = parse_or_die(p, "pick_param($threshold) + 2");
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 7.5);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_function_can_read_variable_argument\n");
}

static void test_ast_function_can_eval_nested_expression(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    cxpr_register_builtins(reg);
    cxpr_registry_add_ast(reg, "double_eval", double_eval_ast_fn, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);

    cxpr_context_set(ctx, "base", 4.0);
    cxpr_ast* ast = parse_or_die(p, "double_eval(3 + base)");
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 14.0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_function_can_eval_nested_expression\n");
}

static void test_ast_function_blocks_ir_compile(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_program* prog;

    cxpr_register_builtins(reg);
    cxpr_registry_add_ast(reg, "pick_param", pick_param_ast_fn, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);
    cxpr_context_set_param(ctx, "threshold", 5.0);

    cxpr_ast* ast = parse_or_die(p, "pick_param($threshold)");
    prog = cxpr_compile(ast, reg, &err);
    assert(prog == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strcmp(err.message, "Function requires AST evaluation") == 0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_function_blocks_ir_compile\n");
}

static void test_ast_function_userdata_cleanup_on_overwrite_and_free(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    g_ast_userdata_free_count = 0;

    int* factor2 = (int*)malloc(sizeof(int));
    int* factor3 = (int*)malloc(sizeof(int));
    assert(factor2 && factor3);
    *factor2 = 2;
    *factor3 = 3;

    cxpr_register_builtins(reg);
    cxpr_registry_add_ast(reg, "scale_eval", scale_eval_ast_fn, 1, 1, CXPR_VALUE_NUMBER,
                          factor2, ast_userdata_free);
    cxpr_registry_add_ast(reg, "scale_eval", scale_eval_ast_fn, 1, 1, CXPR_VALUE_NUMBER,
                          factor3, ast_userdata_free);
    assert(g_ast_userdata_free_count == 1);

    cxpr_ast* ast = parse_or_die(p, "scale_eval(10)");
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 30.0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    assert(g_ast_userdata_free_count == 2);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_function_userdata_cleanup_on_overwrite_and_free\n");
}

static cxpr_value mode_ast_fn(const cxpr_ast* call_ast,
                              const cxpr_context* ctx,
                              const cxpr_registry* reg,
                              void* userdata,
                              cxpr_error* err) {
    (void)userdata;
    (void)reg;
    double v = 0.0;
    if (!cxpr_eval_ast_number(cxpr_ast_function_arg(call_ast, 0), ctx, reg, &v, err)) {
        return cxpr_fv_double(NAN);
    }
    return cxpr_fv_double(v + 100.0);
}

static cxpr_value mode_value_fn(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    return cxpr_fv_double(args[0] + 1.0);
}

static cxpr_value mode_typed_fn(const cxpr_value* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    assert(args[0].type == CXPR_VALUE_NUMBER);
    return cxpr_fv_double(args[0].d * 10.0);
}

static int g_mode_overlay_ast_calls = 0;

static cxpr_value mode_overlay_ast_fn(const cxpr_ast* call_ast,
                                      const cxpr_context* ctx,
                                      const cxpr_registry* reg,
                                      void* userdata,
                                      cxpr_error* err) {
    (void)userdata;
    ++g_mode_overlay_ast_calls;

    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "mode_tf") == 0);

    double v = 0.0;
    if (!cxpr_eval_ast_number(cxpr_ast_function_arg(call_ast, 0), ctx, reg, &v, err)) {
        return cxpr_fv_double(NAN);
    }

    if (cxpr_ast_function_argc(call_ast) == 2) {
        const cxpr_ast* timeframe = cxpr_ast_function_arg(call_ast, 1);
        if (!timeframe || cxpr_ast_type(timeframe) != CXPR_NODE_STRING) {
            if (err) {
                err->code = CXPR_ERR_TYPE_MISMATCH;
                err->message = "mode_tf expects a trailing string timeframe";
            }
            return cxpr_fv_double(NAN);
        }
        if (strcmp(cxpr_ast_string_value(timeframe), "1h") == 0) {
            return cxpr_fv_double(v + 1000.0);
        }
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "Unsupported timeframe";
        }
        return cxpr_fv_double(NAN);
    }

    return cxpr_fv_double(v + 1.0);
}

static cxpr_value source_pick_value_fn(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 1);
    return cxpr_fv_double(args[0] + 1.0);
}

static int g_source_pick_overlay_ast_calls = 0;

static cxpr_value source_pick_overlay_ast_fn(const cxpr_ast* call_ast,
                                             const cxpr_context* ctx,
                                             const cxpr_registry* reg,
                                             void* userdata,
                                             cxpr_error* err) {
    (void)ctx;
    (void)reg;
    (void)userdata;
    ++g_source_pick_overlay_ast_calls;

    assert(cxpr_ast_type(call_ast) == CXPR_NODE_FUNCTION_CALL);
    assert(strcmp(cxpr_ast_function_name(call_ast), "source_pick") == 0);
    assert(cxpr_ast_function_argc(call_ast) == 1);

    const cxpr_ast* arg = cxpr_ast_function_arg(call_ast, 0);
    if (!arg || cxpr_ast_type(arg) != CXPR_NODE_IDENTIFIER) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "source_pick expects one identifier argument";
        }
        return cxpr_fv_double(NAN);
    }

    if (strcmp(cxpr_ast_identifier_name(arg), "close") != 0) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "Unsupported source identifier";
        }
        return cxpr_fv_double(NAN);
    }

    return cxpr_fv_double(42.0);
}

static void trend_struct_producer(const double* args, size_t argc,
                                  cxpr_value* out, size_t field_count,
                                  void* userdata) {
    (void)userdata;
    assert(field_count == 1);
    assert(argc >= 1);
    out[0] = cxpr_fv_double(args[0] + 10.0);
}

static int g_trend_overlay_ast_calls = 0;

static cxpr_value trend_overlay_ast_fn(const cxpr_ast* call_ast,
                                       const cxpr_context* ctx,
                                       const cxpr_registry* reg,
                                       void* userdata,
                                       cxpr_error* err) {
    (void)userdata;
    ++g_trend_overlay_ast_calls;

    assert(cxpr_ast_type(call_ast) == CXPR_NODE_PRODUCER_ACCESS);
    assert(strcmp(cxpr_ast_producer_name(call_ast), "trend_tf") == 0);
    assert(strcmp(cxpr_ast_producer_field(call_ast), "signal") == 0);
    assert(cxpr_ast_producer_argc(call_ast) == 2);

    double v = 0.0;
    if (!cxpr_eval_ast_number(cxpr_ast_producer_arg(call_ast, 0), ctx, reg, &v, err)) {
        return cxpr_fv_double(NAN);
    }

    const cxpr_ast* timeframe = cxpr_ast_producer_arg(call_ast, 1);
    if (!timeframe || cxpr_ast_type(timeframe) != CXPR_NODE_STRING) {
        if (err) {
            err->code = CXPR_ERR_TYPE_MISMATCH;
            err->message = "trend_tf expects a trailing string timeframe";
        }
        return cxpr_fv_double(NAN);
    }
    if (strcmp(cxpr_ast_string_value(timeframe), "1h") == 0) {
        return cxpr_fv_double(v + 2000.0);
    }
    if (err) {
        err->code = CXPR_ERR_SYNTAX;
        err->message = "Unsupported timeframe";
    }
    return cxpr_fv_double(NAN);
}

static bool ir_program_has_opcode(const cxpr_ir_program* prog, cxpr_opcode opcode) {
    for (size_t i = 0; i < prog->count; ++i) {
        if (prog->code[i].op == opcode) return true;
    }
    return false;
}

static void test_registry_overwrite_ast_value_typed_ast_sequence(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;
    cxpr_value typed_out = {0};
    cxpr_value_type arg_types[1] = {CXPR_VALUE_NUMBER};

    cxpr_register_builtins(reg);
    cxpr_ast* ast = parse_or_die(p, "mode(2)");

    /* AST function path: eval works, IR compile is intentionally blocked. */
    cxpr_registry_add_ast(reg, "mode", mode_ast_fn, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 102.0);
    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message && strcmp(err.message, "Function requires AST evaluation") == 0);

    /* Overwrite with value function: should restore regular compile/eval behavior. */
    cxpr_registry_add_value(reg, "mode", mode_value_fn, 1, 1, NULL, NULL);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 3.0);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(cxpr_eval_program_number(prog, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 3.0);
    cxpr_program_free(prog);

    /* Overwrite with typed function: typed path should work unchanged. */
    cxpr_registry_add_typed(reg, "mode", mode_typed_fn, 1, 1, arg_types, CXPR_VALUE_NUMBER, NULL, NULL);
    assert(cxpr_eval_ast(ast, ctx, reg, &typed_out, &err));
    assert(err.code == CXPR_OK);
    assert(typed_out.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(typed_out.d, 20.0);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);
    assert(cxpr_eval_program(prog, ctx, reg, &typed_out, &err));
    assert(err.code == CXPR_OK);
    assert(typed_out.type == CXPR_VALUE_NUMBER);
    ASSERT_DOUBLE_EQ(typed_out.d, 20.0);
    cxpr_program_free(prog);

    /* Overwrite back to AST function: compile must fail again for the same reason. */
    cxpr_registry_add_ast(reg, "mode", mode_ast_fn, 1, 1, CXPR_VALUE_NUMBER, NULL, NULL);
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 102.0);
    prog = cxpr_compile(ast, reg, &err);
    assert(prog == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message && strcmp(err.message, "Function requires AST evaluation") == 0);

    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_registry_overwrite_ast_value_typed_ast_sequence\n");
}

static void test_ast_overlay_numeric_calls_compile_to_scalar_ir(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    cxpr_register_builtins(reg);
    cxpr_registry_add_value(reg, "mode_tf", mode_value_fn, 1, 1, NULL, NULL);
    cxpr_registry_add_ast_overlay(reg, "mode_tf", mode_overlay_ast_fn, 1, 2, NULL, NULL);

    cxpr_ast* ast = parse_or_die(p, "mode_tf(2)");
    g_mode_overlay_ast_calls = 0;
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 3.0);
    assert(g_mode_overlay_ast_calls == 1);

    cxpr_ir_program ir = {0};
    assert(cxpr_ir_compile(ast, reg, &ir, &err));
    assert(ir_program_has_opcode(&ir, CXPR_OP_CALL_FUNC));
    assert(!ir_program_has_opcode(&ir, CXPR_OP_CALL_AST));

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);

    g_mode_overlay_ast_calls = 0;
    assert(cxpr_eval_program_number(prog, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 3.0);
    assert(g_mode_overlay_ast_calls == 0);

    cxpr_ir_program_reset(&ir);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_overlay_numeric_calls_compile_to_scalar_ir\n");
}

static void test_ast_overlay_string_calls_fall_back_to_ast_in_ir(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    cxpr_register_builtins(reg);
    cxpr_registry_add_value(reg, "mode_tf", mode_value_fn, 1, 1, NULL, NULL);
    cxpr_registry_add_ast_overlay(reg, "mode_tf", mode_overlay_ast_fn, 1, 2, NULL, NULL);

    cxpr_ast* ast = parse_or_die(p, "mode_tf(2, \"1h\") + 5");
    g_mode_overlay_ast_calls = 0;
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 1007.0);
    assert(g_mode_overlay_ast_calls == 1);

    cxpr_ir_program ir = {0};
    assert(cxpr_ir_compile(ast, reg, &ir, &err));
    assert(ir_program_has_opcode(&ir, CXPR_OP_CALL_AST));
    assert(ir_program_has_opcode(&ir, CXPR_OP_ADD));

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);

    g_mode_overlay_ast_calls = 0;
    assert(cxpr_eval_program_number(prog, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 1007.0);
    assert(g_mode_overlay_ast_calls == 1);

    cxpr_ir_program_reset(&ir);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_overlay_string_calls_fall_back_to_ast_in_ir\n");
}

static void test_ast_overlay_identifier_calls_fall_back_to_ast_in_ir(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;

    cxpr_register_builtins(reg);
    cxpr_registry_add_value(reg, "source_pick", source_pick_value_fn, 1, 1, NULL, NULL);
    cxpr_registry_add_ast_overlay(reg, "source_pick", source_pick_overlay_ast_fn, 1, 1, NULL, NULL);

    cxpr_ast* ast = parse_or_die(p, "source_pick(close) + 5");
    g_source_pick_overlay_ast_calls = 0;
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 47.0);
    assert(g_source_pick_overlay_ast_calls == 1);

    cxpr_ir_program ir = {0};
    assert(cxpr_ir_compile(ast, reg, &ir, &err));
    assert(ir_program_has_opcode(&ir, CXPR_OP_CALL_AST));
    assert(ir_program_has_opcode(&ir, CXPR_OP_ADD));

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);

    g_source_pick_overlay_ast_calls = 0;
    assert(cxpr_eval_program_number(prog, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 47.0);
    assert(g_source_pick_overlay_ast_calls == 1);

    cxpr_ir_program_reset(&ir);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_ast_overlay_identifier_calls_fall_back_to_ast_in_ir\n");
}

static void test_expression_compile_supports_timeframe_overlay(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_evaluator* evaluator = cxpr_evaluator_new(reg);
    cxpr_error err = {0};
    bool found = false;
    double out = 0.0;

    assert(evaluator != NULL);
    cxpr_register_builtins(reg);
    cxpr_registry_add_value(reg, "mode_tf", mode_value_fn, 1, 1, NULL, NULL);
    cxpr_registry_add_ast_overlay(reg, "mode_tf", mode_overlay_ast_fn, 1, 2, NULL, NULL);
    cxpr_context_set(ctx, "base", 2.0);

    assert(cxpr_expression_add(evaluator, "signal", "mode_tf(base, \"1h\") + 5", &err));
    assert(cxpr_expression_compile(evaluator, &err));
    assert(err.code == CXPR_OK);

    g_mode_overlay_ast_calls = 0;
    cxpr_expression_eval_all(evaluator, ctx, &err);
    assert(err.code == CXPR_OK);
    out = cxpr_expression_get_double(evaluator, "signal", &found);
    assert(found);
    ASSERT_DOUBLE_EQ(out, 1007.0);
    assert(g_mode_overlay_ast_calls == 1);

    cxpr_evaluator_free(evaluator);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    printf("  ✓ test_expression_compile_supports_timeframe_overlay\n");
}

static void test_producer_access_string_calls_fall_back_to_ast_in_ir(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    double out = 0.0;
    const char* fields[] = {"signal"};

    cxpr_register_builtins(reg);
    cxpr_registry_add_struct(reg, "trend_tf", trend_struct_producer, 1, 2, fields, 1, NULL, NULL);
    cxpr_registry_add_ast_overlay(reg, "trend_tf", trend_overlay_ast_fn, 1, 2, NULL, NULL);

    cxpr_ast* ast = parse_or_die(p, "trend_tf(2, \"1h\").signal + 5");
    g_trend_overlay_ast_calls = 0;
    assert(cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 2007.0);
    assert(g_trend_overlay_ast_calls == 1);

    cxpr_ir_program ir = {0};
    assert(cxpr_ir_compile(ast, reg, &ir, &err));
    assert(ir_program_has_opcode(&ir, CXPR_OP_CALL_AST));
    assert(ir_program_has_opcode(&ir, CXPR_OP_ADD));

    cxpr_program* prog = cxpr_compile(ast, reg, &err);
    assert(prog != NULL);

    g_trend_overlay_ast_calls = 0;
    assert(cxpr_eval_program_number(prog, ctx, reg, &out, &err));
    assert(err.code == CXPR_OK);
    ASSERT_DOUBLE_EQ(out, 2007.0);
    assert(g_trend_overlay_ast_calls == 1);

    cxpr_ir_program_reset(&ir);
    cxpr_program_free(prog);
    cxpr_ast_free(ast);
    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(p);
    printf("  ✓ test_producer_access_string_calls_fall_back_to_ast_in_ir\n");
}

int main(void) {
    printf("Running ast function tests...\n");
    test_ast_function_can_read_variable_argument();
    test_ast_function_can_eval_nested_expression();
    test_ast_function_blocks_ir_compile();
    test_ast_function_userdata_cleanup_on_overwrite_and_free();
    test_registry_overwrite_ast_value_typed_ast_sequence();
    test_ast_overlay_numeric_calls_compile_to_scalar_ir();
    test_ast_overlay_string_calls_fall_back_to_ast_in_ir();
    test_ast_overlay_identifier_calls_fall_back_to_ast_in_ir();
    test_expression_compile_supports_timeframe_overlay();
    test_producer_access_string_calls_fall_back_to_ast_in_ir();
    printf("All ast function tests passed!\n");
    return 0;
}
