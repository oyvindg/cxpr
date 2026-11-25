#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/model/compiled.h>
#include <cxpr/model/model.h>

int main(void) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(
        "model append_output\nin { close }\nbase = close > 1\nout base\n", &err);
    cxpr_expr_ast* ast;
    cxpr_model_compiled* program;

    assert(model != NULL);
    ast = cxpr_expr_ast_clone(cxpr_model_binding_expr(model, 0u));
    assert(ast != NULL);
    assert(cxpr_model_append_output_binding(model, "derived", ast, &err));
    assert(cxpr_model_binding_count(model) == 2u);
    assert(cxpr_model_output_count(model) == 2u);
    assert(strcmp(cxpr_model_binding_name(model, 1u), "derived") == 0);
    assert(strcmp(cxpr_model_output(model, 1u), "derived") == 0);
    assert(cxpr_model_binding_expr(model, 1u) == ast);

    program = cxpr_model_compile(model, NULL, &err);
    if (!program) fprintf(stderr, "compile failed: %s\n", err.message);
    assert(program != NULL);
    assert(cxpr_model_compiled_output_count(program) == 2u);
    assert(strcmp(cxpr_model_compiled_output_name(program, 1u), "derived") == 0);
    cxpr_model_compiled_free(program);

    {
        cxpr_expr_ast* duplicate = cxpr_expr_ast_clone(ast);
        assert(duplicate != NULL);
        assert(!cxpr_model_append_output_binding(model, "derived", duplicate, &err));
        cxpr_expr_ast_free(duplicate);
    }
    cxpr_model_free(model);
    puts("model append output binding: all tests passed");
    return 0;
}
