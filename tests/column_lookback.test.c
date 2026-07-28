#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double close;
    double high;
    double signal;
} test_bar;

typedef struct {
    const test_bar* bars;
    size_t count;
    const int64_t* cursor;
} expression_lookback_env;

static void expect_double_eq(double got, double want, const char* label) {
    if (got != want) {
        fprintf(stderr, "%s: got %.17g, want %.17g\n", label, got, want);
        assert(0);
    }
}

static cxpr_ast* parse_or_die(cxpr_parser* parser, const char* source) {
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(parser, source, &err);
    if (!ast) {
        fprintf(stderr, "Parse failed for '%s': %s\n",
                source, err.message ? err.message : "(null)");
        assert(0);
    }
    return ast;
}

static double eval_number_or_die(cxpr_ast* ast, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    double out = NAN;
    if (!cxpr_eval_ast_number(ast, ctx, reg, &out, &err) || err.code != CXPR_OK) {
        char* expression = cxpr_ast_to_string(ast);
        fprintf(stderr, "AST lookback eval failed for '%s': %s\n",
                expression ? expression : "(null)",
                err.message ? err.message : "(no message)");
        free(expression);
        assert(0);
    }
    return out;
}

static double eval_program_number_or_die(cxpr_ast* ast, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    cxpr_program* program = cxpr_compile(ast, reg, &err);
    double out = NAN;
    if (!program || err.code != CXPR_OK) {
        fprintf(stderr, "IR lookback compile failed: %s\n",
                err.message ? err.message : "(no message)");
        assert(0);
    }
    if (!cxpr_eval_program_number(program, ctx, reg, &out, &err) || err.code != CXPR_OK) {
        fprintf(stderr, "IR lookback eval failed: %s\n",
                err.message ? err.message : "(no message)");
        assert(0);
    }
    cxpr_program_free(program);
    return out;
}

static bool eval_bool_or_die(cxpr_ast* ast, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    bool out = false;
    if (!cxpr_eval_ast_bool(ast, ctx, reg, &out, &err) || err.code != CXPR_OK) {
        fprintf(stderr, "AST bool lookback eval failed: %s\n",
                err.message ? err.message : "(no message)");
        assert(0);
    }
    return out;
}

static bool eval_program_bool_or_die(cxpr_ast* ast, cxpr_context* ctx, cxpr_registry* reg) {
    cxpr_error err = {0};
    cxpr_program* program = cxpr_compile(ast, reg, &err);
    bool out = false;
    if (!program || err.code != CXPR_OK) {
        fprintf(stderr, "IR bool lookback compile failed: %s\n",
                err.message ? err.message : "(no message)");
        assert(0);
    }
    if (!cxpr_eval_program_bool(program, ctx, reg, &out, &err) || err.code != CXPR_OK) {
        fprintf(stderr, "IR bool lookback eval failed: %s\n",
                err.message ? err.message : "(no message)");
        assert(0);
    }
    cxpr_program_free(program);
    return out;
}

static bool expression_lookback_resolver(const cxpr_ast* target,
                                         const cxpr_ast* index,
                                         const cxpr_context* ctx,
                                         const cxpr_registry* reg,
                                         void* userdata,
                                         cxpr_value* out,
                                         cxpr_error* err) {
    const expression_lookback_env* env = (const expression_lookback_env*)userdata;
    cxpr_context* shifted;
    double offset_value;
    int64_t offset;
    int64_t shifted_index;

    (void)ctx;
    if (!env || !target || !index || !out || !env->cursor) return false;
    if (cxpr_ast_type(index) != CXPR_NODE_NUMBER) return false;
    offset_value = cxpr_ast_number_value(index);
    if (offset_value < 0.0) return false;
    offset = (int64_t)offset_value;
    shifted_index = *env->cursor - offset;
    if (shifted_index < 0 || (size_t)shifted_index >= env->count) {
        *out = cxpr_num(NAN);
        return true;
    }

    shifted = cxpr_context_new();
    assert(shifted != NULL);
    cxpr_context_set(shifted, "close", env->bars[shifted_index].close);
    cxpr_context_set(shifted, "high", env->bars[shifted_index].high);
    cxpr_context_set(shifted, "signal", env->bars[shifted_index].signal);
    if (!cxpr_eval_ast(target, shifted, reg, out, err)) {
        cxpr_context_free(shifted);
        return false;
    }
    cxpr_context_free(shifted);
    return true;
}

static double test_ema(const double* args, size_t argc, void* userdata) {
    (void)userdata;
    assert(argc == 2u);
    return args[0] - (args[1] * 0.05);
}

static void test_column_lookback_resolves_bound_columns(void) {
    static const test_bar bars[] = {
        {10.0, 11.0, 1.0},
        {20.0, 22.0, 2.0},
        {30.0, 33.0, 3.0},
        {40.0, 44.0, 4.0},
    };
    int64_t cursor = 3;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 4},
        {"high", &bars[0].high, sizeof(bars[0]), 4},
        {"signal", &bars[0].signal, sizeof(bars[0]), 4},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 3, &cursor));

    ast = parse_or_die(parser, "close[1] + high[2]");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 52.0, "AST close[1] + high[2]");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 52.0, "IR close[1] + high[2]");
    cxpr_ast_free(ast);

    cursor = 2;
    ast = parse_or_die(parser, "close[0] + high[1]");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 52.0, "AST close[0] + high[1]");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 52.0, "IR close[0] + high[1]");
    cxpr_ast_free(ast);

    cursor = 3;
    ast = parse_or_die(parser, "close[1][2]");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 10.0, "AST close[1][2]");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 10.0, "IR close[1][2]");
    cxpr_ast_free(ast);

    ast = parse_or_die(parser, "signal[2]");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 2.0, "AST signal[2]");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 2.0, "IR signal[2]");
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_returns_nan_for_warmup_or_out_of_range(void) {
    static const test_bar bars[] = {
        {10.0, 11.0, 1.0},
        {20.0, 22.0, 2.0},
    };
    int64_t cursor = 0;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 2},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;
    double out;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 1, &cursor));

    ast = parse_or_die(parser, "close[1]");
    out = eval_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    out = eval_program_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    cxpr_ast_free(ast);

    cursor = 4;
    ast = parse_or_die(parser, "close[0]");
    out = eval_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    out = eval_program_number_or_die(ast, ctx, reg);
    assert(isnan(out));
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_expression_lookback_matches_ast_and_ir(void) {
    static const test_bar bars[] = {
        {10.0, 11.0, 1.0},
        {20.0, 18.0, 2.0},
        {30.0, 33.0, 3.0},
        {40.0, 36.0, 4.0},
    };
    int64_t cursor = 3;
    expression_lookback_env env = {bars, 4u, &cursor};
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;

    assert(parser);
    assert(ctx);
    assert(reg);
    cxpr_registry_set_lookback_resolver(reg, expression_lookback_resolver, &env, NULL);
    cxpr_registry_add(reg, "ema", test_ema, 2u, 2u, NULL, NULL);

    ast = parse_or_die(parser, "(close + high)[1]");
    expect_double_eq(eval_number_or_die(ast, ctx, reg),
                     63.0,
                     "AST (close + high)[1]");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg),
                     63.0,
                     "IR (close + high)[1]");
    cxpr_ast_free(ast);

    ast = parse_or_die(parser, "(close > high)[1]");
    assert(eval_bool_or_die(ast, ctx, reg) == false);
    assert(eval_program_bool_or_die(ast, ctx, reg) == false);
    cxpr_ast_free(ast);

    cursor = 2;
    ast = parse_or_die(parser, "(close > high)[1]");
    assert(eval_bool_or_die(ast, ctx, reg) == true);
    assert(eval_program_bool_or_die(ast, ctx, reg) == true);
    cxpr_ast_free(ast);

    cursor = 3;
    ast = parse_or_die(parser, "(close > ema(close, 20))[2]");
    assert(eval_bool_or_die(ast, ctx, reg) == true);
    assert(eval_program_bool_or_die(ast, ctx, reg) == true);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_leaves_unknown_or_dynamic_targets_unresolved(void) {
    static const test_bar bars[] = {
        {10.0, 11.0, 1.0},
        {20.0, 22.0, 2.0},
    };
    int64_t cursor = 1;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 2},
    };
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    cxpr_ast* ast;
    double out = NAN;

    assert(parser);
    assert(ctx);
    assert(reg);
    assert(cxpr_register_column_lookback(reg, columns, 1, &cursor));

    ast = parse_or_die(parser, "open[0]");
    assert(!cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code != CXPR_OK);
    cxpr_ast_free(ast);

    err = (cxpr_error){0};
    out = NAN;
    cxpr_context_set_param(ctx, "n", 1.0);
    ast = parse_or_die(parser, "close[$n]");
    assert(!cxpr_eval_ast_number(ast, ctx, reg, &out, &err));
    assert(err.code != CXPR_OK);
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_defined_function_struct_arg_preserves_series_lookback(void) {
    static const test_bar bars[] = {
        {10.0, 11.0, 1.0},
        {20.0, 55.0, 2.0},
        {30.0, 33.0, 3.0},
        {40.0, 44.0, 4.0},
    };
    int64_t cursor = 3;
    cxpr_lookback_column columns[] = {
        {"close", &bars[0].close, sizeof(bars[0]), 4},
        {"high", &bars[0].high, sizeof(bars[0]), 4},
    };
    const char* session_fields[] = {"bar_index"};
    cxpr_value session_values[] = {cxpr_num(1.0)};
    cxpr_struct_value* session = cxpr_struct_value_new(session_fields, session_values, 1u);
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_ast* ast;

    assert(parser);
    assert(ctx);
    assert(reg);
    cxpr_register_defaults(reg);
    assert(session);
    assert(cxpr_register_column_lookback(reg, columns, 2, &cursor));
    cxpr_context_set_struct(ctx, "session", session);
    cxpr_struct_value_free(session);
    assert(cxpr_registry_define_fn(
               reg,
               "prior_for_session(close, high, session) => "
               "session.bar_index == 1 ? close[2] + high[1] : 0").code == CXPR_OK);
    assert(cxpr_registry_define_fn(
               reg,
               "peak_for_session(high, session, bars) => "
               "session.bar_index == 1 ? window_highest(high, bars) : 0").code == CXPR_OK);

    ast = parse_or_die(parser, "prior_for_session(close, high, session)");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 53.0,
                     "AST defined fn preserves series lookback");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 53.0,
                     "IR defined fn preserves series lookback");
    cxpr_ast_free(ast);

    ast = parse_or_die(parser, "peak_for_session(high, session, 3)");
    expect_double_eq(eval_number_or_die(ast, ctx, reg), 55.0,
                     "AST defined fn preserves window series arg");
    expect_double_eq(eval_program_number_or_die(ast, ctx, reg), 55.0,
                     "IR defined fn preserves window series arg");
    cxpr_ast_free(ast);

    cxpr_registry_free(reg);
    cxpr_context_free(ctx);
    cxpr_parser_free(parser);
}

static void test_column_lookback_rejects_invalid_registration(void) {
    cxpr_registry* reg = cxpr_registry_new();
    assert(reg);

    assert(!cxpr_register_column_lookback(NULL, NULL, 0, NULL));
    assert(!cxpr_register_column_lookback(reg, NULL, 1, NULL));
    assert(cxpr_register_column_lookback(reg, NULL, 0, NULL));

    cxpr_registry_free(reg);
}

int main(void) {
    printf("Running column lookback tests...\n");
    test_column_lookback_resolves_bound_columns();
    test_column_lookback_returns_nan_for_warmup_or_out_of_range();
    test_expression_lookback_matches_ast_and_ir();
    test_column_lookback_leaves_unknown_or_dynamic_targets_unresolved();
    test_defined_function_struct_arg_preserves_series_lookback();
    test_column_lookback_rejects_invalid_registration();
    printf("All column lookback tests passed!\n");
    return 0;
}
