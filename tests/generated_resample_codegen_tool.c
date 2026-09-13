#include <cxpr/cxpr.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static cxpr_value unused(const cxpr_expr_ast* ast, const cxpr_context* ctx,
                         const cxpr_registry* reg, void* ud, cxpr_error* err) {
    (void)ast; (void)ctx; (void)reg; (void)ud; (void)err;
    return (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = 0.0};
}

int main(int argc, char** argv) {
    cxpr_error err = {0};
    cxpr_registry* reg = cxpr_registry_new();
    cxpr_model* model;
    cxpr_model_compiled* program;
    char* source;
    FILE* out;
    if (argc != 2 || !reg) return 2;
    cxpr_registry_add_ast(reg, "resample", unused, 2u, 2u, CXPR_VALUE_NUMBER, NULL, NULL);
    model = cxpr_model_parse("model generated_resample\nin { close }\ncurrent_twice = resample(close, \"1h\") + resample(close, \"1h\")\nprevious_twice = resample(close, \"1h\")[1] + resample(close, \"1h\")[1]\nfive_minute = resample(close, \"5m\")\ntotal = current_twice + previous_twice + five_minute\nout total\n", &err);
    program = model ? cxpr_model_compile(model, reg, &err) : NULL;
    assert(program);
    cxpr_model_free(model);
    model = NULL;
    assert(cxpr_model_compiled_resample_requirement_count(program) == 2u);
    assert(strcmp(cxpr_model_compiled_resample_requirement_interval(program, 0u), "1h") == 0);
    assert(strcmp(cxpr_model_compiled_resample_requirement_interval(program, 1u), "5m") == 0);
    source = program ? cxpr_model_compiled_generate_c(program, "static", "generated_resample_tick", &err) : NULL;
    out = source ? fopen(argv[1], "wb") : NULL;
    if (!out) { fprintf(stderr, "%s\n", err.message ? err.message : "resample codegen failed"); return 1; }
    fputs(source, out); fclose(out); free(source);
    cxpr_model_compiled_free(program); cxpr_registry_free(reg);
    return 0;
}
