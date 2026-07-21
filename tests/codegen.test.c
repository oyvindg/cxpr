/**
 * @file codegen.test.c
 * @brief Tests for cxpr_ast_to_c / cxpr_exprset_to_c.
 */

#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static char* dup_text(const char* text) {
    size_t len = strlen(text);
    char* out = (char*)malloc(len + 1u);
    assert(out);
    memcpy(out, text, len + 1u);
    return out;
}

static char* to_c(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    assert(ast && err.code == CXPR_OK);
    char* out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out && err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return out;
}

static char* program_to_c(const char* expr,
                          const cxpr_c_program_arg* args,
                          size_t arg_count) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_program* program;
    char* out;
    assert(ast && err.code == CXPR_OK);
    assert(reg != NULL);
    cxpr_register_defaults(reg);
    program = cxpr_compile(ast, reg, &err);
    assert(program && err.code == CXPR_OK);
    out = cxpr_program_to_c_function(program, "static inline", "double",
                                     "eval_expr", args, arg_count, &err);
    assert(out && err.code == CXPR_OK);
    cxpr_program_free(program);
    cxpr_registry_free(reg);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return out;
}

static char* test_emit_leaf_at_offset(const cxpr_ast* ast,
                                      unsigned lookback_offset,
                                      void* userdata,
                                      cxpr_error* err) {
    char buf[128];
    const char* name = NULL;
    (void)userdata;
    if (cxpr_ast_type(ast) == CXPR_NODE_IDENTIFIER) {
        name = cxpr_ast_identifier_name(ast);
    } else if (cxpr_ast_type(ast) == CXPR_NODE_FIELD_ACCESS) {
        snprintf(buf, sizeof(buf), "%s_%s[(i >= %uu ? i - %uu : 0u)]",
                 cxpr_ast_field_object(ast),
                 cxpr_ast_field_name(ast),
                 lookback_offset,
                 lookback_offset);
        return dup_text(buf);
    }
    if (!name) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "test leaf hook only supports identifiers and fields";
        }
        return NULL;
    }
    if (lookback_offset == 0u) {
        snprintf(buf, sizeof(buf), "%s[i]", name);
    } else {
        snprintf(buf, sizeof(buf), "%s[(i >= %uu ? i - %uu : 0u)]",
                 name,
                 lookback_offset,
                 lookback_offset);
    }
    return dup_text(buf);
}

static char* to_c_with_lookback(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = test_emit_leaf_at_offset,
    };
    assert(ast && err.code == CXPR_OK);
    char* out = cxpr_ast_to_c(ast, &target, &err);
    assert(out && err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return out;
}

/* ── emit_call_at_offset hook ────────────────────────────────────────────── */

typedef struct call_hook_ud {
    const cxpr_c_target* self;
} call_hook_ud;

/* Handles `rsi(...)` as an opaque offset-aware leaf (a precomputed state var),
 * `wrap(x)` by recursing into its argument at the current offset (proving
 * cxpr_ast_to_c_at_offset threads the offset), and falls through otherwise. */
static char* test_emit_call(const cxpr_ast* ast,
                            unsigned lookback_offset,
                            void* userdata,
                            bool* handled,
                            cxpr_error* err) {
    const char* name = cxpr_ast_function_name(ast);
    call_hook_ud* ud = (call_hook_ud*)userdata;
    char buf[256];

    if (name && strcmp(name, "rsi") == 0) {
        *handled = true;
        if (lookback_offset == 0u) {
            snprintf(buf, sizeof(buf), "rsi_val[i]");
        } else {
            snprintf(buf, sizeof(buf), "rsi_val[(i >= %uu ? i - %uu : 0u)]",
                     lookback_offset, lookback_offset);
        }
        return dup_text(buf);
    }
    if (name && strcmp(name, "wrap") == 0) {
        *handled = true;
        char* inner = cxpr_ast_to_c_at_offset(
            cxpr_ast_function_arg(ast, 0u), lookback_offset, ud->self, err);
        if (!inner) return NULL;
        snprintf(buf, sizeof(buf), "W(%s)", inner);
        free(inner);
        return dup_text(buf);
    }
    *handled = false;
    return NULL;
}

static char* to_c_with_call(const char* expr) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, expr, &err);
    call_hook_ud ud = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = test_emit_leaf_at_offset,
        .emit_call_at_offset = test_emit_call,
        .userdata = &ud,
    };
    ud.self = &target;
    assert(ast && err.code == CXPR_OK);
    char* out = cxpr_ast_to_c(ast, &target, &err);
    assert(out && err.code == CXPR_OK);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    return out;
}

static void call_eq(const char* expr, const char* want) {
    char* got = to_c_with_call(expr);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "to_c_with_call(\"%s\") = \"%s\", want \"%s\"\n", expr, got, want);
        assert(0);
    }
    free(got);
}

static void test_emit_call_hook(void) {
    /* handled opaque call -> precomputed state var leaf */
    call_eq("rsi(close, 14)", "rsi_val[i]");
    /* lookback applies to the handled call's offset */
    call_eq("rsi(close, 14)[1] > 30", "(rsi_val[(i >= 1u ? i - 1u : 0u)] > 30.0)");
    /* recursion via cxpr_ast_to_c_at_offset threads the offset into the arg */
    call_eq("wrap(close)", "W(close[i])");
    call_eq("wrap(close)[2]", "W(close[(i >= 2u ? i - 2u : 0u)])");
    /* unhandled call falls through to cxpr's own emission (leaf hook on arg) */
    call_eq("sqrt(x)", "sqrt(x[i])");
    /* unhandled builtin still gets cxpr's rising/falling expansion */
    call_eq("falling(close, 2)", "((close[i] < close[(i >= 1u ? i - 1u : 0u)]))");
    printf("  emit_call_at_offset hook OK\n");
}

static void eq(const char* expr, const char* want) {
    char* got = to_c(expr);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "to_c(\"%s\") = \"%s\", want \"%s\"\n", expr, got, want);
        assert(0);
    }
    free(got);
}

static void test_operators(void) {
    eq("a + b", "(a + b)");
    eq("a * b + c", "((a * b) + c)");
    eq("a^2", "pow(a, 2.0)");          /* power -> pow */
    eq("c ^ 2", "pow(c, 2.0)");
    eq("a % b", "fmod(a, b)");          /* % -> fmod */
    eq("a and b or c", "((a && b) || c)");
    eq("not a", "(!a)");
    eq("-x", "(-x)");
    eq("a < b", "(a < b)");
    eq("x ? a : b", "(x ? a : b)");
    eq("$thr", "thr");                  /* $param -> bare name */
    printf("  operators OK\n");
}

static void test_number_literals_are_double_literals(void) {
    eq("2 / (period + 1)", "(2.0 / (period + 1.0))");
    printf("  number literal formatting OK\n");
}

static void test_functions(void) {
    eq("sqrt(x)", "sqrt(x)");
    eq("abs(x)", "fabs(x)");            /* abs -> fabs */
    eq("hypot(a, b)", "hypot(a, b)");
    eq("min(a, b)", "fmin(a, b)");      /* variadic min -> nested fmin */
    eq("min(a, b, c)", "fmin(fmin(a, b), c)");
    eq("max(a, b, c)", "fmax(fmax(a, b), c)");
    printf("  functions OK\n");
}

static void test_lookback_codegen_with_leaf_hook(void) {
    char* out = to_c_with_lookback("close[2] > open[1]");
    assert(strcmp(out, "(close[(i >= 2u ? i - 2u : 0u)] > open[(i >= 1u ? i - 1u : 0u)])") == 0);
    free(out);

    out = to_c_with_lookback("close[1][2]");
    assert(strcmp(out, "close[(i >= 3u ? i - 3u : 0u)]") == 0);
    free(out);

    out = to_c_with_lookback("falling(close, 3)");
    assert(strcmp(out, "((close[i] < close[(i >= 1u ? i - 1u : 0u)]) && (close[(i >= 1u ? i - 1u : 0u)] < close[(i >= 2u ? i - 2u : 0u)]))") == 0);
    free(out);

    out = to_c_with_lookback("repeat(condition=close > open, bars=2)");
    assert(strcmp(out, "(((close[i] > open[i])) && ((close[(i >= 1u ? i - 1u : 0u)] > open[(i >= 1u ? i - 1u : 0u)])))") == 0);
    free(out);

    printf("  lookback leaf-hook codegen OK\n");
}

static void test_membership_desugar(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "s in [1, 2]", &err);
    char* printed;
    char* out;

    assert(ast && err.code == CXPR_OK);
    printed = cxpr_ast_to_string(ast);
    assert(printed && strcmp(printed, "contains(s, [1, 2])") == 0);
    free(printed);

    err = (cxpr_error){0};
    out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL && err.code != CXPR_OK);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  membership desugar OK\n");
}

static void test_unsupported(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};

    /* unknown function with no mapping -> error */
    cxpr_ast* ast = cxpr_parse(p, "mystery(x)", &err);
    assert(ast);
    err = (cxpr_error){0};
    char* out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL && err.code != CXPR_OK);
    cxpr_ast_free(ast);

    ast = cxpr_parse(p, "close[1]", &err);
    assert(ast);
    err = (cxpr_error){0};
    out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL && err.code != CXPR_OK);
    cxpr_ast_free(ast);

    cxpr_parser_free(p);
    printf("  unsupported rejected OK\n");
}

static void test_typecheck_rejection(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "not 1", &err);
    char* out;

    assert(ast && err.code == CXPR_OK);
    err = (cxpr_error){0};
    out = cxpr_ast_to_c(ast, NULL, &err);
    assert(out == NULL);
    assert(err.code == CXPR_ERR_TYPE_MISMATCH);

    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  typecheck rejection OK\n");
}

static void test_exprset_topo(void) {
    /* Interdependent set, declared out of order. Must emit in dependency
     * order: r_s before f before dr_dl. */
    const char* names[] = { "dr_dl", "f", "r_s" };
    const char* srcs[]  = { "p_r * f", "1 - r_s / r", "2 * G * M / c^2" };

    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_c_named_expr defs[3];
    cxpr_ast* asts[3];
    for (int i = 0; i < 3; ++i) {
        asts[i] = cxpr_parse(p, srcs[i], &err);
        assert(asts[i] && err.code == CXPR_OK);
        defs[i].name = names[i];
        defs[i].ast = asts[i];
    }

    char* block = cxpr_exprset_to_c(defs, 3, "double", NULL, &err);
    assert(block && err.code == CXPR_OK);

    /* r_s defined before f, f before dr_dl. */
    char* p_rs = strstr(block, "double r_s");
    char* p_f  = strstr(block, "double f ");
    char* p_dr = strstr(block, "double dr_dl");
    assert(p_rs && p_f && p_dr);
    assert(p_rs < p_f && p_f < p_dr);
    /* power transpiled inside the block too */
    assert(strstr(block, "pow(c, 2.0)"));

    free(block);
    for (int i = 0; i < 3; ++i) cxpr_ast_free(asts[i]);
    cxpr_parser_free(p);
    printf("  exprset topo-order OK\n");
}

static void test_exprset_cycle(void) {
    const char* names[] = { "a", "b" };
    const char* srcs[]  = { "b + 1", "a + 1" }; /* a<->b cycle */
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_c_named_expr defs[2];
    cxpr_ast* asts[2];
    for (int i = 0; i < 2; ++i) { asts[i] = cxpr_parse(p, srcs[i], &err); assert(asts[i]); defs[i].name = names[i]; defs[i].ast = asts[i]; }

    err = (cxpr_error){0};
    char* block = cxpr_exprset_to_c(defs, 2, "double", NULL, &err);
    assert(block == NULL && err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);

    for (int i = 0; i < 2; ++i) cxpr_ast_free(asts[i]);
    cxpr_parser_free(p);
    printf("  exprset cycle detected OK\n");
}

static void test_exprset_to_c_function(void) {
    /* Interdependent set, declared out of order; host supplies all names/types. */
    const char* names[] = { "dr", "f", "r_s" };
    const char* srcs[]  = { "p_r * f", "1 - r_s / r", "2 * G * M / c^2" };
    const char* inputs[] = { "r", "p_r", "G", "M", "c" };

    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_c_named_expr defs[3];
    cxpr_ast* asts[3];
    for (int i = 0; i < 3; ++i) {
        asts[i] = cxpr_parse(p, srcs[i], &err);
        assert(asts[i]);
        defs[i].name = names[i];
        defs[i].ast = asts[i];
    }

    char* code = cxpr_exprset_to_c_function("static inline", "State", "double", "eval",
                                            inputs, 5, defs, 3, NULL, &err);
    assert(code && err.code == CXPR_OK);

    /* struct with a field per expression, function with a param per input */
    assert(strstr(code, "typedef struct State {"));
    assert(strstr(code, "double r_s;") && strstr(code, "double f;") && strstr(code, "double dr;"));
    assert(strstr(code, "static inline State eval(double r, double p_r, double G, double M, double c)"));
    /* dependency-ordered locals + power transpiled */
    char* d_rs = strstr(code, "double r_s = ");
    char* d_f  = strstr(code, "double f = ");
    char* d_dr = strstr(code, "double dr = ");
    assert(d_rs && d_f && d_dr && d_rs < d_f && d_f < d_dr);
    assert(strstr(code, "pow(c, 2.0)"));
    /* packs results into the struct and returns it */
    assert(strstr(code, "_cx_out.dr = dr;") && strstr(code, "return _cx_out;"));

    free(code);
    for (int i = 0; i < 3; ++i) cxpr_ast_free(asts[i]);
    cxpr_parser_free(p);
    printf("  exprset_to_c_function OK\n");
}

static void test_program_to_c_function(void) {
    cxpr_c_program_arg args[] = {
        {.kind = CXPR_C_PROGRAM_ARG_VAR, .name = "close"},
        {.kind = CXPR_C_PROGRAM_ARG_PARAM, .name = "limit"},
    };
    char* code = program_to_c("if(close > $limit, close - $limit, 0)", args, 2u);
    assert(strstr(code, "static inline double eval_expr(double close, double p_limit)"));
    assert(strstr(code, "goto L"));
    assert(strstr(code, "return _cx_s[--_cx_sp];"));
    free(code);
    printf("  program_to_c_function OK\n");
}

static void test_program_to_c_function_requires_explicit_bindings(void) {
    cxpr_parser* p = cxpr_parser_new();
    cxpr_error err = {0};
    cxpr_ast* ast = cxpr_parse(p, "close + 1", &err);
    cxpr_program* program;
    char* code;
    assert(ast && err.code == CXPR_OK);
    program = cxpr_compile(ast, NULL, &err);
    assert(program && err.code == CXPR_OK);
    code = cxpr_program_to_c_function(program, NULL, NULL, "missing", NULL, 0u, &err);
    assert(!code);
    assert(err.code == CXPR_ERR_UNKNOWN_IDENTIFIER);
    cxpr_program_free(program);
    cxpr_ast_free(ast);
    cxpr_parser_free(p);
    printf("  program_to_c_function explicit bindings OK\n");
}

static void test_defined_fn_to_c_function(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    char* code;
    assert(reg);
    err = cxpr_registry_define_fn(reg, "rsi(avg_gain, avg_loss) => if(avg_loss == 0, 100, 100 - (100 / (1 + avg_gain / avg_loss)))");
    assert(err.code == CXPR_OK);
    code = cxpr_registry_defined_fn_to_c_function(reg, "rsi", "static inline",
                                                  "double", "cxpr_fn_rsi", &err);
    assert(code && err.code == CXPR_OK);
    assert(strstr(code, "static inline double cxpr_fn_rsi(double avg_gain, double avg_loss)"));
    assert(strstr(code, "goto L"));
    assert(!strstr(code, "cxpr_registry"));
    free(code);
    cxpr_registry_free(reg);
    printf("  defined_fn_to_c_function OK\n");
}

static void test_defined_fn_to_c_function_builtin_calls(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    char* code;

    assert(reg != NULL);
    cxpr_register_defaults(reg);
    err = cxpr_registry_define_fn(reg, "round_period(x) => max(1, round(x))");
    assert(err.code == CXPR_OK);
    code = cxpr_registry_defined_fn_to_c_function(reg, "round_period", "static inline",
                                                  "double", "cxpr_fn_round_period", &err);
    assert(code != NULL);
    assert(err.code == CXPR_OK);
    assert(strstr(code, "round(") != NULL);
    assert(strstr(code, "fmax(") != NULL);
    free(code);
    cxpr_registry_free(reg);
    printf("  defined_fn_to_c_function builtin calls OK\n");
}

static void test_defined_fn_to_c_function_minmax(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    char* code;

    assert(reg != NULL);
    cxpr_register_defaults(reg);
    err = cxpr_registry_define_fn(
        reg,
        "risk(close, ref_close, peak_high, floor_pct) = "
        "((peak_high - ref_close) / max(peak_high, close * floor_pct)) > 0.2");
    assert(err.code == CXPR_OK);
    code = cxpr_registry_defined_fn_to_c_function(
        reg,
        "risk",
        "static inline",
        "double",
        "cxpr_fn_risk",
        &err);
    assert(code != NULL);
    assert(err.code == CXPR_OK);
    assert(strstr(code, "fmax(") != NULL);
    assert(strstr(code, "Unsupported") == NULL);
    free(code);
    cxpr_registry_free(reg);
    printf("  defined_fn_to_c_function minmax OK\n");
}

static void test_defined_fn_to_c_function_rejects_unknown(void) {
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_error err = {0};
    char* code;
    assert(reg);
    code = cxpr_registry_defined_fn_to_c_function(reg, "missing", NULL,
                                                  NULL, "missing", &err);
    assert(!code);
    assert(err.code == CXPR_ERR_UNKNOWN_FUNCTION);
    cxpr_registry_free(reg);
    printf("  defined_fn_to_c_function unknown OK\n");
}

int main(void) {
    printf("codegen tests:\n");
    test_operators();
    test_number_literals_are_double_literals();
    test_functions();
    test_lookback_codegen_with_leaf_hook();
    test_emit_call_hook();
    test_membership_desugar();
    test_unsupported();
    test_typecheck_rejection();
    test_exprset_topo();
    test_exprset_cycle();
    test_exprset_to_c_function();
    test_program_to_c_function();
    test_program_to_c_function_requires_explicit_bindings();
    test_defined_fn_to_c_function();
    test_defined_fn_to_c_function_builtin_calls();
    test_defined_fn_to_c_function_minmax();
    test_defined_fn_to_c_function_rejects_unknown();
    printf("All codegen tests passed.\n");
    return 0;
}
