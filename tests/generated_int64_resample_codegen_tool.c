#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static cxpr_value unused(const cxpr_expr_ast* ast, const cxpr_context* ctx,
                         const cxpr_registry* reg, void* userdata,
                         cxpr_error* err) {
    (void)ast; (void)ctx; (void)reg; (void)userdata; (void)err;
    return cxpr_int64(0);
}

int main(int argc, char** argv) {
    cxpr_error err = {0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_model* model;
    cxpr_model_compiled* program;
    char* source;
    FILE* out;
    if (argc != 2 || !reg) return 2;
    cxpr_registry_add_ast(reg, "resample", unused, 2u, 2u,
                          CXPR_VALUE_INT64, NULL, NULL);
    model = cxpr_model_parse(
        "model generated_int64_resample\n"
        "in { id: int64 }\n"
        "total = resample(id, \"1h\") + resample(id, \"1h\")[1]\n"
        "out total\n", &err);
    program = model ? cxpr_model_compile(model, reg, &err) : NULL;
    assert(program);
    assert(cxpr_model_compiled_resample_requirement_result_kind(program, 0u) ==
           CXPR_MODEL_RESULT_INT64);
    source = cxpr_model_compiled_generate_c(
        program, "static", "generated_int64_resample_tick", &err);
    out = source ? fopen(argv[1], "wb") : NULL;
    if (!out) return 1;
    fputs(source, out);
    fclose(out);
    free(source);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(reg);
    return 0;
}
