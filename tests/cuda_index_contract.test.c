#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cxpr_model_compiled* compile_model(const char* source,
                                          cxpr_model** out_model,
                                          cxpr_error* err) {
    cxpr_model* model = cxpr_model_parse(source, err);
    cxpr_model_compiled* program;
    assert(model);
    program = cxpr_model_compile(model, NULL, err);
    assert(program);
    *out_model = model;
    return program;
}

static void test_cuda_supports_neutral_scalar_history(void) {
    cxpr_model* model = NULL;
    cxpr_error err = {0};
    cxpr_model_compiled* program = compile_model(
        "model cuda_history\n"
        "in { close }\n"
        "up = close > close[1]\n"
        "out up\n",
        &model, &err);
    char* source = cxpr_cuda_plugin_source_from_program(program, NULL, &err);
    assert(source && err.code == CXPR_OK);
    assert(strstr(source, "cxpr_history") != NULL);
    assert(strstr(source, "__device__") != NULL);
    cxpr_cuda_plugin_source_free(source);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

static void test_cuda_rejects_dynamic_aggregate_index(void) {
    cxpr_model* model = NULL;
    cxpr_error err = {0};
    cxpr_model_compiled* program = compile_model(
        "model cuda_dynamic_array\n"
        "$index = 1\n"
        "value = [10, 20][$index]\n"
        "out value\n",
        &model, &err);
    char* source = cxpr_cuda_plugin_source_from_program(program, NULL, &err);
    assert(source == NULL);
    assert(err.code != CXPR_OK);
    assert(err.message != NULL);
    assert(strcmp(err.message,
                  "Model C backend requires runnable fused scalar IR fallback") == 0);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

int main(void) {
    test_cuda_supports_neutral_scalar_history();
    test_cuda_rejects_dynamic_aggregate_index();
    puts("CUDA neutral index contract tests passed");
    return 0;
}
