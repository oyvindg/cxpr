#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool contains_name(const char* const* names, size_t count, const char* want) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (strcmp(names[i], want) == 0) return true;
    }
    return false;
}

static bool contains_producer_field(const cxpr_expr_ast_producer_field_ref* refs,
                                    size_t count,
                                    const char* producer_name,
                                    const char* field_name) {
    size_t i;
    for (i = 0; i < count; ++i) {
        if (strcmp(refs[i].producer_name, producer_name) == 0 &&
            strcmp(refs[i].field_name, field_name) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct cxpr_test_expr_def {
    const char* key;
    const char* expr;
    cxpr_expr_ast* ast;
} cxpr_test_expr_def;

static const cxpr_test_expr_def* find_expr_def(
    const cxpr_test_expr_def* defs,
    size_t count,
    const char* key) {
    size_t i;

    for (i = 0u; i < count; ++i) {
        if (strcmp(defs[i].key, key) == 0) return &defs[i];
    }
    return NULL;
}

static size_t add_unique_context(
    const char** out,
    size_t count,
    size_t cap,
    const char* context) {
    size_t i;

    if (!context) return count;
    for (i = 0u; i < count && i < cap; ++i) {
        if (strcmp(out[i], context) == 0) return count;
    }
    if (count < cap) out[count] = context;
    return count + 1u;
}

static bool expr_def_contains_variable_recursive(
    const cxpr_test_expr_def* defs,
    size_t def_count,
    const cxpr_test_expr_def* def,
    const char* variable,
    const char** visited,
    size_t visited_count) {
    const char* refs[16];
    size_t ref_count;
    size_t i;

    if (!def || !variable) return false;
    for (i = 0u; i < visited_count; ++i) {
        if (strcmp(visited[i], def->key) == 0) return false;
    }
    if (cxpr_expr_ast_contains_variable(def->ast, variable)) return true;
    ref_count = cxpr_expr_ast_references(def->ast, refs, 16u);
    for (i = 0u; i < ref_count && i < 16u; ++i) {
        const cxpr_test_expr_def* child = find_expr_def(defs, def_count, refs[i]);
        const char* next_visited[16];
        if (!child || visited_count + 1u > 16u) continue;
        if (visited_count > 0u) {
            memcpy(next_visited, visited, visited_count * sizeof(next_visited[0]));
        }
        next_visited[visited_count] = def->key;
        if (expr_def_contains_variable_recursive(
                defs, def_count, child, variable, next_visited, visited_count + 1u)) {
            return true;
        }
    }
    return false;
}

static size_t trace_variable_contexts_recursive(
    const cxpr_test_expr_def* defs,
    size_t def_count,
    const cxpr_test_expr_def* def,
    const char* variable,
    const char** out,
    size_t out_count,
    size_t out_cap,
    const char** visited,
    size_t visited_count) {
    const char* contexts[8];
    const char* refs[16];
    size_t context_count;
    size_t ref_count;
    size_t i;

    if (!def || !variable) return out_count;
    for (i = 0u; i < visited_count; ++i) {
        if (strcmp(visited[i], def->key) == 0) return out_count;
    }

    context_count =
        cxpr_expr_ast_call_arg_contexts_for_variable(def->ast, variable, contexts, 8u);
    for (i = 0u; i < context_count && i < 8u; ++i) {
        out_count = add_unique_context(out, out_count, out_cap, contexts[i]);
    }

    ref_count = cxpr_expr_ast_references(def->ast, refs, 16u);
    for (i = 0u; i < ref_count && i < 16u; ++i) {
        const cxpr_test_expr_def* child = find_expr_def(defs, def_count, refs[i]);
        const char* next_visited[16];
        const char* ref_contexts[8];
        size_t ref_context_count;
        size_t j;

        if (!child || visited_count + 1u > 16u) continue;
        if (!expr_def_contains_variable_recursive(
                defs, def_count, child, variable, visited, visited_count)) {
            continue;
        }
        ref_context_count =
            cxpr_expr_ast_call_arg_contexts_for_reference(def->ast, refs[i], ref_contexts, 8u);
        for (j = 0u; j < ref_context_count && j < 8u; ++j) {
            out_count = add_unique_context(out, out_count, out_cap, ref_contexts[j]);
        }
        if (visited_count > 0u) {
            memcpy(next_visited, visited, visited_count * sizeof(next_visited[0]));
        }
        next_visited[visited_count] = def->key;
        out_count = trace_variable_contexts_recursive(
            defs,
            def_count,
            child,
            variable,
            out,
            out_count,
            out_cap,
            next_visited,
            visited_count + 1u);
    }
    return out_count;
}

static void test_reference_extractors_cover_split_reference_logic(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_error err = {0};
    const char* refs[8];
    const char* fns[8];
    const char* vars[8];
    size_t ref_count;
    size_t fn_count;
    size_t var_count;

    assert(parser);
    ast = cxpr_expr_ast_parse(parser, "quote.mid + pose.velocity.x + clamp(close, $lo, $hi) + $base.period", &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    ref_count = cxpr_expr_ast_references(ast, refs, 8);
    fn_count = cxpr_expr_ast_functions_used(ast, fns, 8);
    var_count = cxpr_expr_ast_variables_used(ast, vars, 8);

    assert(ref_count >= 3);
    assert(fn_count == 1);
    assert(var_count == 3);
    assert(contains_name(refs, ref_count, "quote.mid"));
    assert(contains_name(refs, ref_count, "pose.velocity.x"));
    assert(contains_name(refs, ref_count, "close"));
    assert(contains_name(fns, fn_count, "clamp"));
    assert(contains_name(vars, var_count, "lo"));
    assert(contains_name(vars, var_count, "hi"));
    assert(contains_name(vars, var_count, "base.period"));

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_producer_field_extractors_collect_unique_pairs(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_error err = {0};
    cxpr_expr_ast_producer_field_ref refs[8];
    size_t ref_count;

    assert(parser);
    ast = cxpr_expr_ast_parse(
        parser,
        "ichimoku(9, 26, 52).tenkan > ichimoku(9, 26, 52).senkouA and adx(14).adx > 20",
        &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    ref_count = cxpr_expr_ast_producer_fields_used(ast, refs, 8);

    assert(ref_count == 3u);
    assert(contains_producer_field(refs, ref_count, "ichimoku", "tenkan"));
    assert(contains_producer_field(refs, ref_count, "ichimoku", "senkouA"));
    assert(contains_producer_field(refs, ref_count, "adx", "adx"));

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_call_arg_contexts_trace_references_and_params(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_error err = {0};
    const char* contexts[8];
    size_t context_count;

    assert(parser);
    ast = cxpr_expr_ast_parse(
        parser,
        "supertrend(period=10, mult=ema(atr_pct, $atr_baseline)).value",
        &err);
    assert(ast);
    assert(cxpr_expr_ast_contains_reference(ast, "atr_pct"));
    assert(cxpr_expr_ast_contains_variable(ast, "atr_baseline"));
    assert(!cxpr_expr_ast_contains_variable(ast, "missing"));

    context_count =
        cxpr_expr_ast_call_arg_contexts_for_reference(ast, "atr_pct", contexts, 8u);
    assert(context_count == 2u);
    assert(contains_name(contexts, context_count, "supertrend"));
    assert(contains_name(contexts, context_count, "ema"));

    context_count =
        cxpr_expr_ast_call_arg_contexts_for_variable(ast, "atr_baseline", contexts, 8u);
    assert(context_count == 2u);
    assert(contains_name(contexts, context_count, "supertrend"));
    assert(contains_name(contexts, context_count, "ema"));

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_call_arg_contexts_report_multiple_consumers(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_expr_ast* ast;
    cxpr_error err = {0};
    const char* contexts[8];
    size_t context_count;

    assert(parser);
    ast = cxpr_expr_ast_parse(
        parser,
        "supertrend(period=10, mult=base).value + macd(base, 26, 9).line",
        &err);
    assert(ast);

    context_count =
        cxpr_expr_ast_call_arg_contexts_for_reference(ast, "base", contexts, 8u);
    assert(context_count == 2u);
    assert(contains_name(contexts, context_count, "supertrend"));
    assert(contains_name(contexts, context_count, "macd"));

    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
}

static void test_call_arg_contexts_support_indirect_alias_trace(void) {
    cxpr_expr_parser* parser = cxpr_expr_parser_new();
    cxpr_error err = {0};
    cxpr_test_expr_def defs[] = {
        {"base", "ema(atr_pct, $atr_baseline)", NULL},
        {"st_mult", "base", NULL},
        {"st", "supertrend(period=10, mult=st_mult)", NULL},
    };
    const char* contexts[8];
    const char* visited[1];
    size_t context_count;
    size_t i;

    assert(parser);
    for (i = 0u; i < sizeof(defs) / sizeof(defs[0]); ++i) {
        defs[i].ast = cxpr_expr_ast_parse(parser, defs[i].expr, &err);
        assert(defs[i].ast != NULL);
    }

    context_count = trace_variable_contexts_recursive(
        defs,
        sizeof(defs) / sizeof(defs[0]),
        &defs[2],
        "atr_baseline",
        contexts,
        0u,
        8u,
        visited,
        0u);
    assert(context_count == 2u);
    assert(contains_name(contexts, context_count, "supertrend"));
    assert(contains_name(contexts, context_count, "ema"));

    for (i = 0u; i < sizeof(defs) / sizeof(defs[0]); ++i) {
        cxpr_expr_ast_free(defs[i].ast);
    }
    cxpr_expr_parser_free(parser);
}

int main(void) {
    test_reference_extractors_cover_split_reference_logic();
    test_producer_field_extractors_collect_unique_pairs();
    test_call_arg_contexts_trace_references_and_params();
    test_call_arg_contexts_report_multiple_consumers();
    test_call_arg_contexts_support_indirect_alias_trace();
    printf("  \xE2\x9C\x93 ast_references\n");
    return 0;
}
