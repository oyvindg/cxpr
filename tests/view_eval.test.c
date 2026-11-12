#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include <cxpr/cxpr.h>

static cxpr_expr_ast* parse(const char* source, cxpr_expr_parser** out_parser) {
    cxpr_error err = {0};
    *out_parser = cxpr_expr_parser_new();
    assert(*out_parser);
    cxpr_expr_ast* ast = cxpr_expr_ast_parse(*out_parser, source, &err);
    assert(ast && err.code == CXPR_OK);
    return ast;
}

int main(void) {
    double samples[] = {1.0, 2.0, 7.5, 4.0};
    cxpr_view_requirement requirement = {0};
    cxpr_view binding = {0};
    cxpr_view_resolved resolved = {0};
    cxpr_view_table table = {&resolved, 1u, 42u};
    cxpr_context* context = cxpr_context_new();
    cxpr_expr_parser* parser = NULL;
    cxpr_expr_ast* ast;
    cxpr_value result = {0};
    cxpr_error err = {0};
    cxpr_registry* registry = cxpr_registry_new();

    assert(registry != NULL);
    cxpr_register_defaults(registry);

    requirement.name = "samples";
    requirement.element_type = CXPR_VALUE_NUMBER;
    requirement.rank = 1u;
    requirement.max_shape[0] = 16u;
    requirement.min_alignment = _Alignof(double);
    requirement.access = CXPR_VIEW_ACCESS_READ_ONLY;
    binding.data = samples;
    binding.element_type = CXPR_VALUE_NUMBER;
    binding.rank = 1u;
    binding.shape[0] = 4u;
    binding.strides[0] = (ptrdiff_t)sizeof(double);
    binding.capacity_bytes = sizeof(samples);
    binding.flags = CXPR_VIEW_ACCESS_READ_ONLY | CXPR_VIEW_CONTIGUOUS;

    assert(cxpr_views_resolve(
        &requirement, &binding, 1u, &resolved, NULL) == CXPR_VIEW_OK);
    assert(cxpr_context_bind_views(
        context, &requirement, &table, 1u) == CXPR_VIEW_OK);

    ast = parse("samples[2]", &parser);
    assert(cxpr_eval_ast(ast, context, NULL, &result, &err));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 7.5);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    err = cxpr_registry_define_fn(
        registry,
        "fn get_clamped(values, index) = "
        "get(values, clamp(index, 0, length(values, 0) - 1), 0)");
    assert(err.code == CXPR_OK);
    ast = parse("get_clamped(samples, 8)", &parser);
    if (!cxpr_eval_ast(ast, context, registry, &result, &err)) {
        fprintf(stderr, "defined get_clamped failed: %d %s\n",
                (int)err.code, err.message ? err.message : "(no message)");
        assert(false);
    }
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 4.0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    ast = parse("samples[8]", &parser);
    assert(!cxpr_eval_ast(ast, context, NULL, &result, &err));
    assert(err.code == CXPR_ERR_INDEX_OUT_OF_RANGE);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    err = (cxpr_error){0};
    ast = parse("get(samples, 8, -3.5)", &parser);
    assert(cxpr_eval_ast(ast, context, NULL, &result, &err));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == -3.5);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    err = (cxpr_error){0};
    ast = parse("get_clamped(samples, 8)", &parser);
    assert(cxpr_eval_ast(ast, context, registry, &result, &err));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 4.0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);

    err = (cxpr_error){0};
    ast = parse("get_clamped(samples, 1.5)", &parser);
    assert(cxpr_eval_ast(ast, context, registry, &result, &err));
    assert(result.type == CXPR_VALUE_NUMBER && result.d == 0.0);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    cxpr_context_free(context);
    cxpr_registry_free(registry);

    puts("external view evaluator tests passed");
    return 0;
}
