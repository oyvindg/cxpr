#include <cxpr/cxpr.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct resample_fixture {
    const double* values;
    size_t count;
    int64_t cursor;
    unsigned resolver_calls;
} resample_fixture;

static char* duplicate_text(const char* text) {
    size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static int is_hourly_close(const cxpr_expr_ast* ast) {
    const cxpr_expr_ast* source;
    const cxpr_expr_ast* every;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        strcmp(cxpr_expr_ast_call_name(ast), "resample") != 0 ||
        cxpr_expr_ast_call_arg_count(ast) != 2u) {
        return 0;
    }
    source = cxpr_expr_ast_call_arg(ast, 0u);
    every = cxpr_expr_ast_call_arg(ast, 1u);
    return source && every &&
           cxpr_expr_ast_kind_of(source) == CXPR_NODE_IDENTIFIER &&
           strcmp(cxpr_expr_ast_identifier_name(source), "close") == 0 &&
           cxpr_expr_ast_kind_of(every) == CXPR_NODE_STRING &&
           strcmp(cxpr_expr_ast_string_value(every), "1h") == 0;
}

static bool resolve_resampled_lookback(const cxpr_expr_ast* target,
                                       const cxpr_expr_ast* index,
                                       const cxpr_context* context,
                                       const cxpr_registry* registry,
                                       void* userdata,
                                       cxpr_value* out,
                                       cxpr_error* err) {
    resample_fixture* fixture = (resample_fixture*)userdata;
    double raw_index = 0.0;
    int64_t offset;
    (void)context;
    (void)registry;
    if (!is_hourly_close(target)) return false;
    assert(cxpr_expr_ast_kind_of(index) == CXPR_NODE_NUMBER);
    raw_index = cxpr_expr_ast_number_value(index);
    offset = (int64_t)raw_index;
    assert(raw_index == (double)offset && offset >= 0);
    fixture->resolver_calls++;
    if (fixture->cursor < offset || fixture->cursor - offset >= (int64_t)fixture->count) {
        if (err) {
            err->code = CXPR_ERR_INDEX_OUT_OF_RANGE;
            err->message = "resampled lookback outside fixture history";
        }
        return true;
    }
    *out = (cxpr_value){
        .type = CXPR_VALUE_NUMBER,
        .d = fixture->values[fixture->cursor - offset],
    };
    return true;
}

static cxpr_value evaluate_resample(const cxpr_expr_ast* call_ast,
                                    const cxpr_context* context,
                                    const cxpr_registry* registry,
                                    void* userdata,
                                    cxpr_error* err) {
    resample_fixture* fixture = (resample_fixture*)userdata;
    (void)context;
    (void)registry;
    if (!is_hourly_close(call_ast) || fixture->cursor < 0 ||
        fixture->cursor >= (int64_t)fixture->count) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "unknown resampled series";
        }
        return (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = NAN};
    }
    return (cxpr_value){
        .type = CXPR_VALUE_NUMBER,
        .d = fixture->values[fixture->cursor],
    };
}

static char* emit_resample_call(const cxpr_expr_ast* ast,
                                unsigned lookback,
                                void* userdata,
                                bool* handled,
                                cxpr_error* err) {
    char expression[192];
    (void)userdata;
    if (!is_hourly_close(ast)) {
        *handled = false;
        return NULL;
    }
    *handled = true;
    if (lookback == 0u) {
        snprintf(expression, sizeof(expression), "close_1h[cursor]");
    } else {
        snprintf(expression, sizeof(expression),
                 "(cursor >= %uu ? close_1h[cursor - %uu] : NAN)",
                 lookback, lookback);
    }
    if (err) *err = (cxpr_error){0};
    return duplicate_text(expression);
}

static void test_tree_ir_and_backend_offset_parity(void) {
    static const double close_1h[] = {100.0, 104.0, 103.0, 108.0};
    resample_fixture fixture = {
        .values = close_1h,
        .count = sizeof(close_1h) / sizeof(close_1h[0]),
        .cursor = 3,
    };
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast;
    cxpr_expr_compiled* ir;
    cxpr_value tree = {0};
    cxpr_value compiled = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_call_at_offset = emit_resample_call,
    };
    char* generated;

    assert(parser && registry && context);
    cxpr_registry_add_ast(registry, "resample", evaluate_resample, 2u, 2u,
                          CXPR_VALUE_NUMBER, &fixture, NULL);
    cxpr_registry_set_lookback_resolver(registry, resolve_resampled_lookback,
                                        &fixture, NULL);
    ast = cxpr_expr_ast_parse(parser,
        "resample(close, every=\"1h\") > resample(close, \"1h\")[1]",
        &err);
    assert(ast && err.code == CXPR_OK);
    assert(cxpr_eval_ast(ast, context, registry, &tree, &err));
    assert(tree.type == CXPR_VALUE_BOOL && tree.b);

    ir = cxpr_expr_compile(ast, registry, &err);
    assert(ir && err.code == CXPR_OK);
    assert(cxpr_expr_compiled_eval(ir, context, registry, &compiled, &err));
    assert(compiled.type == CXPR_VALUE_BOOL && compiled.b == tree.b);
    assert(fixture.resolver_calls == 2u);

    generated = cxpr_expr_ast_to_c(ast, &target, &err);
    assert(generated && err.code == CXPR_OK);
    assert(strcmp(generated,
        "(close_1h[cursor] > (cursor >= 1u ? close_1h[cursor - 1u] : NAN))") == 0);
    /* The target contract is C-like: this expression is valid in generated C
       and CUDA device code, with arrays/alignment supplied before launch. */
    assert(strstr(generated, "resample(") == NULL);
    assert(strstr(generated, "cursor - 1u") != NULL);

    free(generated);
    cxpr_expr_compiled_free(ir);
    cxpr_expr_ast_free(ast);
    cxpr_context_free(context);
    cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

typedef struct bound_views_fixture {
    const double* values[2];
    size_t counts[2];
    int64_t cursors[2];
} bound_views_fixture;

static int bound_slot(const cxpr_expr_ast* ast) {
    const cxpr_expr_ast* every;
    const char* interval;
    if (!ast || cxpr_expr_ast_kind_of(ast) != CXPR_NODE_FUNCTION_CALL ||
        strcmp(cxpr_expr_ast_call_name(ast), "resample") != 0 ||
        cxpr_expr_ast_call_arg_count(ast) != 2u) return -1;
    every = cxpr_expr_ast_call_arg(ast, 1u);
    interval = every && cxpr_expr_ast_kind_of(every) == CXPR_NODE_STRING
        ? cxpr_expr_ast_string_value(every) : NULL;
    if (interval && strcmp(interval, "1h") == 0) return 0;
    if (interval && strcmp(interval, "5m") == 0) return 1;
    return -1;
}

static bool resolve_bound_view(const cxpr_expr_ast* target,
                               const cxpr_expr_ast* index,
                               const cxpr_context* context,
                               const cxpr_registry* registry,
                               void* userdata, cxpr_value* out,
                               cxpr_error* err) {
    bound_views_fixture* fixture = userdata;
    int slot = bound_slot(target);
    int64_t offset;
    (void)context; (void)registry; (void)err;
    if (slot < 0) return false;
    offset = (int64_t)cxpr_expr_ast_number_value(index);
    *out = (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = NAN};
    if (fixture->cursors[slot] >= offset &&
        fixture->cursors[slot] - offset < (int64_t)fixture->counts[slot]) {
        out->d = fixture->values[slot][fixture->cursors[slot] - offset];
    }
    return true;
}

static cxpr_value evaluate_bound_view(const cxpr_expr_ast* ast,
                                      const cxpr_context* context,
                                      const cxpr_registry* registry,
                                      void* userdata, cxpr_error* err) {
    bound_views_fixture* fixture = userdata;
    int slot = bound_slot(ast);
    (void)context; (void)registry; (void)err;
    if (slot < 0 || fixture->cursors[slot] < 0 ||
        fixture->cursors[slot] >= (int64_t)fixture->counts[slot])
        return (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = NAN};
    return (cxpr_value){.type = CXPR_VALUE_NUMBER,
                        .d = fixture->values[slot][fixture->cursors[slot]]};
}

static void test_bound_views_tree_ir_multiple_and_missing(void) {
    static const double hourly[] = {100.0, 104.0, 103.0};
    static const double five_minute[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    bound_views_fixture fixture = {
        .values = {hourly, five_minute}, .counts = {3u, 5u},
        .cursors = {2, 4},
    };
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_context* context = cxpr_context_new();
    cxpr_error err = {0};
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(
        parser, "resample(close, \"1h\")[1] + resample(close, \"5m\")", &err);
    cxpr_expr_compiled* ir;
    cxpr_value tree = {0}, compiled = {0};
    assert(ast && registry && context);
    cxpr_registry_add_ast(registry, "resample", evaluate_bound_view, 2u, 2u,
                          CXPR_VALUE_NUMBER, &fixture, NULL);
    cxpr_registry_set_lookback_resolver(registry, resolve_bound_view, &fixture, NULL);
    ir = cxpr_expr_compile(ast, registry, &err);
    assert(ir);
    assert(cxpr_eval_ast(ast, context, registry, &tree, &err));
    assert(cxpr_expr_compiled_eval(ir, context, registry, &compiled, &err));
    assert(tree.d == 109.0 && compiled.d == tree.d);
    fixture.cursors[0] = 0; /* [1] warmup/missing in the hourly slot. */
    assert(cxpr_eval_ast(ast, context, registry, &tree, &err));
    assert(cxpr_expr_compiled_eval(ir, context, registry, &compiled, &err));
    assert(isnan(tree.d) && isnan(compiled.d));
    cxpr_expr_compiled_free(ir); cxpr_expr_ast_free(ast);
    cxpr_context_free(context); cxpr_registry_free(registry);
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_tree_ir_and_backend_offset_parity();
    test_bound_views_tree_ir_multiple_and_missing();
    puts("resample tree/IR/generated-C/CUDA offset contract OK");
    return 0;
}
