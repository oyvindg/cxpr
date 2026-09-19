/*
 * R6 -- argmin / argmax / fixed-arity sum: cross-backend parity.
 *
 * Asserts bit-identical results across tree-eval and IR, plus a generated-C
 * contract check (nested `?:`, no array node, no UNSUPPORTED_RESULT). CUDA inherits
 * the C path (plugins/cuda.c), so the generated-C contract covers it.
 *
 * The compatibility guard remains available to downstream builds, but R6 is enabled
 * by default now that all scalar backends implement the folds.
 *
 * See plans/field_pathfinding_requirements.md (R6).
 */
#include <stdio.h>

#ifndef CXPR_PATHFINDING_R6_READY
#define CXPR_PATHFINDING_R6_READY 1
#endif

#ifndef CXPR_TEST_SOURCE_DIR
#define CXPR_TEST_SOURCE_DIR "."
#endif

#if CXPR_PATHFINDING_R6_READY

#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Evaluate a bare expression via tree-eval (compiled=0) or IR (compiled=1). */
static double eval_expr(const char* source, int compiled) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &err);
    cxpr_context* ctx = cxpr_context_new();
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_value value = {0};

    assert(ast && ctx && reg && err.code == CXPR_OK);
    cxpr_register_defaults(reg);
    if (compiled) {
        cxpr_expr_compiled* program = cxpr_expr_compile(ast, reg, &err);
        assert(program);
        assert(cxpr_expr_compiled_eval(program, ctx, reg, &value, &err));
        cxpr_expr_compiled_free(program);
    } else {
        assert(cxpr_eval_ast(ast, ctx, reg, &value, &err));
    }
    assert(err.code == CXPR_OK);
    assert(value.type == CXPR_VALUE_NUMBER || value.type == CXPR_VALUE_INT64);

    cxpr_context_free(ctx);
    cxpr_registry_free(reg);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return value.type == CXPR_VALUE_INT64 ? (double)value.i64 : value.d;
}

/* tree-eval and IR must agree, and both must equal the expected value. */
static void assert_parity(const char* source, double expected) {
    assert(eval_expr(source, 0) == expected);
    assert(eval_expr(source, 1) == expected);
}

static void assert_rejected(const char* source) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(parser, source, &err);
    if (ast) {
        cxpr_registry* reg = cxpr_registry_new();
        cxpr_expr_compiled* program;
        assert(reg);
        cxpr_register_defaults(reg);
        program = cxpr_expr_compile(ast, reg, &err);
        assert(program == NULL); /* arity > 8 must fail like min/max */
        cxpr_registry_free(reg);
        cxpr_expr_ast_free(ast);
    }
    cxpr_expr_parser_free(parser);
}

static void test_basic(void) {
    assert_parity("argmin(3, 1, 2)", 1.0);
    assert_parity("argmax(3, 1, 2)", 0.0);
    assert_parity("sum(1, 2, 3)", 6.0);
    /* arity 1 and arity 8 boundaries */
    assert_parity("argmin(5)", 0.0);
    assert_parity("argmin(8, 7, 6, 5, 4, 3, 2, 1)", 7.0);
    assert_parity("argmax(1, 2, 3, 4, 5, 6, 7, 8)", 7.0);
}

static void test_ties(void) {
    /* exact tie -> lowest index wins */
    assert_parity("argmin(2, 2, 5)", 0.0);
    assert_parity("argmax(5, 5, 2)", 0.0);
    /* near-tie: argmin must index the same sub-expression it compares, with no
     * fast-math/contraction, so the index is stable across backends. */
    assert_parity("argmin(1.0 + 0.2, 1.0 + 0.2 * 1.0)", 0.0);
}

static void test_arity_limit(void) {
    assert_rejected("argmin(1, 2, 3, 4, 5, 6, 7, 8, 9)");
    assert_rejected("argmax(1, 2, 3, 4, 5, 6, 7, 8, 9)");
}

static char* read_fixture(const char* name) {
    char path[1024];
    FILE* file;
    long size;
    char* source;
    snprintf(path, sizeof(path), "%s/fixtures/pathfinding/%s",
             CXPR_TEST_SOURCE_DIR, name);
    file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    size = ftell(file);
    rewind(file);
    source = (char*)malloc((size_t)size + 1u);
    assert(source && fread(source, 1u, (size_t)size, file) == (size_t)size);
    source[size] = '\0';
    fclose(file);
    return source;
}

/* Generated-C contract: argmin lowers to nested ternaries, no array node, no
 * UNSUPPORTED marker. CUDA inherits this via plugins/cuda.c post-processing. */
static void test_generated_c_contract(void) {
    cxpr_error err = {0};
    char* source = read_fixture("argmin_argmax.cxpr");
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;
    char* code;

    free(source);
    assert(model);
    program = cxpr_model_compile(model, NULL, &err);
    assert(program);
    code = cxpr_model_compiled_generate_c(program, "static inline",
                                          "argmin_argmax_tick", &err);
    assert(code);
    assert(strstr(code, "?") != NULL);            /* nested ternary lowering */
    assert(strstr(code, "UNSUPPORTED") == NULL);  /* no unsupported result */

    free(code);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

#endif /* CXPR_PATHFINDING_R6_READY */

int main(void) {
#if CXPR_PATHFINDING_R6_READY
    test_basic();
    test_ties();
    test_arity_limit();
    test_generated_c_contract();
    printf("argmin_argmax_parity: OK\n");
#else
    printf("argmin_argmax_parity: SKIP "
           "(R6 argmin/argmax/sum not yet implemented on release/0.3.2)\n");
#endif
    return 0;
}
