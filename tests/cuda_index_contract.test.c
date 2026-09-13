#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t count_text(const char* source, const char* needle) {
    size_t count = 0u;
    size_t length = strlen(needle);
    while ((source = strstr(source, needle)) != NULL) {
        ++count;
        source += length;
    }
    return count;
}

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

static cxpr_value unused_resample(const cxpr_expr_ast* ast,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  void* userdata,
                                  cxpr_error* err) {
    (void)ast; (void)ctx; (void)reg; (void)userdata; (void)err;
    return (cxpr_value){.type = CXPR_VALUE_NUMBER, .d = 0.0};
}

static void test_cuda_resample_view_lowering(void) {
    cxpr_error err = {0};
    cxpr_registry* registry = cxpr_registry_new();
    cxpr_model* model = cxpr_model_parse(
        "model cuda_resample\n"
        "in { close, path }\n"
        "current_twice = resample(close, \"1h\") + resample(close, \"1h\")\n"
        "previous_twice = resample(close, \"1h\")[1] + resample(close, \"1h\")[1]\n"
        "fast = resample(close, \"5m\")\n"
        "daily = resample(path, \"1d\")\n"
        "total = current_twice + previous_twice + fast + daily\n"
        "out total\n", &err);
    cxpr_model_compiled* program;
    char* source;
    assert(registry && model);
    cxpr_registry_add_ast(registry, "resample", unused_resample, 2u, 2u,
                          CXPR_VALUE_NUMBER, NULL, NULL);
    program = cxpr_model_compile(model, registry, &err);
    assert(program);
    /* Current/[1] share 1h; 5m and 1d retain independent view slots. */
    assert(cxpr_model_compiled_resample_requirement_count(program) == 3u);
    assert(strcmp(cxpr_model_compiled_resample_requirement_source(program, 2u),
                  "path") == 0);
    source = cxpr_cuda_plugin_source_from_program(program, NULL, &err);
    assert(source && err.code == CXPR_OK);
    assert(strstr(source, "cxpr_resample_view") != NULL);
    assert(strstr(source, "CXPR_RESAMPLE_VIEW_ABI_VERSION 1u") != NULL);
    assert(strstr(source, "CXPR_RESAMPLE_VIEW_VALUE_TYPE 1u") != NULL);
    assert(strstr(source, "_cx_primary_cursor") != NULL);
    assert(strstr(source, ".alignment[_cx_primary_cursor]") != NULL);
    assert(strstr(source, ".values[") != NULL);
    assert(strstr(source, "_cx_resample_cursor_0_0") != NULL);
    assert(strstr(source, "_cx_resample_value_0_0") != NULL);
    assert(strstr(source, "_cx_resample_cursor_0_1") != NULL);
    assert(strstr(source, "_cx_resample_value_0_1") != NULL);
    assert(count_text(source,
        "_cx_resample_views[0].alignment[_cx_primary_cursor]") == 2u);
    assert(count_text(source, "_cx_resample_views[0].values[") == 2u);
    assert(strstr(source, "_cx_resample_views[1]") != NULL);
    assert(strstr(source, "_cx_resample_views[2]") != NULL);
    assert(strstr(source, "_cx_resample_views[3]") == NULL);
    assert(strstr(source, "requirement_handle") == NULL);
    /* The original .cxpr may remain in comments; executable source must not. */
    assert(strstr(source, " = resample(") == NULL);
    assert(strstr(source, "return resample(") == NULL);
    assert(strstr(source, "__device__") != NULL);
    assert(strstr(source, "__restrict__") != NULL);
    {
        const char* output_path = getenv("CXPR_CUDA_SOURCE_OUT");
        if (output_path && output_path[0]) {
            FILE* output = fopen(output_path, "wb");
            assert(output);
            assert(fputs(source, output) >= 0);
            assert(fclose(output) == 0);
        }
    }
    cxpr_cuda_plugin_source_free(source);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
    cxpr_registry_free(registry);
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
    test_cuda_resample_view_lowering();
    test_cuda_rejects_dynamic_aggregate_index();
    puts("CUDA neutral index contract tests passed");
    return 0;
}
