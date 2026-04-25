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

static bool contains_producer_field(const cxpr_producer_field_ref* refs,
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

static void test_reference_extractors_cover_split_reference_logic(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_ast* ast;
    cxpr_error err = {0};
    const char* refs[8];
    const char* fns[8];
    const char* vars[8];
    size_t ref_count;
    size_t fn_count;
    size_t var_count;

    assert(parser);
    ast = cxpr_parse(parser, "quote.mid + pose.velocity.x + clamp(close, $lo, $hi)", &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    ref_count = cxpr_ast_references(ast, refs, 8);
    fn_count = cxpr_ast_functions_used(ast, fns, 8);
    var_count = cxpr_ast_variables_used(ast, vars, 8);

    assert(ref_count >= 3);
    assert(fn_count == 1);
    assert(var_count == 2);
    assert(contains_name(refs, ref_count, "quote.mid"));
    assert(contains_name(refs, ref_count, "pose.velocity.x"));
    assert(contains_name(refs, ref_count, "close"));
    assert(contains_name(fns, fn_count, "clamp"));
    assert(contains_name(vars, var_count, "lo"));
    assert(contains_name(vars, var_count, "hi"));

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

static void test_producer_field_extractors_collect_unique_pairs(void) {
    cxpr_parser* parser = cxpr_parser_new();
    cxpr_ast* ast;
    cxpr_error err = {0};
    cxpr_producer_field_ref refs[8];
    size_t ref_count;

    assert(parser);
    ast = cxpr_parse(
        parser,
        "ichimoku(9, 26, 52).tenkan > ichimoku(9, 26, 52).senkouA and adx(14).adx > 20",
        &err);
    assert(ast);
    assert(err.code == CXPR_OK);

    ref_count = cxpr_ast_producer_fields_used(ast, refs, 8);

    assert(ref_count == 3u);
    assert(contains_producer_field(refs, ref_count, "ichimoku", "tenkan"));
    assert(contains_producer_field(refs, ref_count, "ichimoku", "senkouA"));
    assert(contains_producer_field(refs, ref_count, "adx", "adx"));

    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
}

int main(void) {
    test_reference_extractors_cover_split_reference_logic();
    test_producer_field_extractors_collect_unique_pairs();
    printf("  \xE2\x9C\x93 ast_references\n");
    return 0;
}
