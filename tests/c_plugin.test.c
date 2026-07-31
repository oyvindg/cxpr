#include <cxpr/cxpr.h>
#include <cxpr/plugins/c.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_complete_artifact_descriptor_metadata(void) {
    static const char source[] =
        "model artifact_test\n"
        "in { close, enabled }\n"
        "$threshold = 3\n"
        "above = enabled and close > $threshold\n"
        "scaled = close * 2\n"
        "out { above, scaled }\n";
    const cxpr_model_host_binding host_bindings[] = {
        {"close", CXPR_MODEL_RESULT_NUMBER},
        {"enabled", CXPR_MODEL_RESULT_BOOL},
    };
    const cxpr_model_compile_options compile_options = {
        .backend = CXPR_MODEL_BACKEND_C,
        .fuse = true,
        .enable_trace = false,
        .host_bindings = host_bindings,
        .host_binding_count = 2u,
    };
    const size_t selected_outputs[] = {1u};
    const cxpr_c_plugin_options options = {
        "artifact_tick",
        "static",
        NULL,
        0u,
        selected_outputs,
        1u,
        1,
    };
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;
    char* artifact;

    assert(model != NULL);
    program = cxpr_model_compile_with_options(
        model, NULL, &compile_options, &err);
    assert(program != NULL);

    artifact = cxpr_c_plugin_artifact_from_program(
        program, cxpr_model_name(model), &options, &err);
    assert(artifact != NULL);
    assert(strstr(artifact, "void artifact_tick(") != NULL);
    assert(strstr(artifact,
                  "static const cxpr_generated_model_descriptor "
                  "artifact_tick_descriptor") != NULL);
    assert(strstr(artifact, ".name = \"artifact_test\"") != NULL);
    assert(strstr(artifact, "return sizeof(artifact_tick_state);") != NULL);
    assert(strstr(artifact, "memset(state, 0, sizeof(artifact_tick_state));") != NULL);
    assert(strstr(artifact, ".param_names[0] = \"threshold\"") != NULL);
    assert(strstr(artifact, ".param_types[0] = CXPR_GENERATED_VALUE_NUMBER") != NULL);
    assert(strstr(artifact, ".param_defaults[0] = 3") != NULL);
    assert(strstr(artifact, ".param_has_default[0] = 1u") != NULL);
    assert(strstr(artifact, ".input_names[0] = \"close\"") != NULL);
    assert(strstr(artifact, ".input_types[0] = CXPR_GENERATED_VALUE_NUMBER") != NULL);
    assert(strstr(artifact, ".input_names[1] = \"enabled\"") != NULL);
    assert(strstr(artifact, ".input_types[1] = CXPR_GENERATED_VALUE_BOOL") != NULL);
    assert(strstr(artifact, ".output_count = 1") != NULL);
    assert(strstr(artifact, ".output_names[0] = \"scaled\"") != NULL);
    assert(strstr(artifact, ".output_types[0] = CXPR_GENERATED_VALUE_NUMBER") != NULL);
    assert(strstr(artifact, ".output_names[1]") == NULL);
    assert(strstr(artifact,
                  ".abi_version = CXPR_GENERATED_MODEL_ABI_VERSION") != NULL);

    cxpr_c_plugin_source_free(artifact);
    cxpr_model_compiled_free(program);
    cxpr_model_free(model);
}

static void test_host_binding_schema_diagnostics(void) {
    static const char source[] =
        "model host_schema\n"
        "in { value, enabled }\n"
        "out result = enabled and value > 0\n";
    const cxpr_model_host_binding incomplete[] = {
        {"value", CXPR_MODEL_RESULT_NUMBER},
    };
    const cxpr_model_host_binding mismatched[] = {
        {"value", CXPR_MODEL_RESULT_NUMBER},
        {"missing", CXPR_MODEL_RESULT_BOOL},
    };
    cxpr_model_compile_options options = {
        .backend = CXPR_MODEL_BACKEND_C,
        .fuse = true,
        .host_bindings = incomplete,
        .host_binding_count = 1u,
    };
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* program;

    assert(model);
    program = cxpr_model_compile_with_options(model, NULL, &options, &err);
    assert(!program);
    assert(err.code != CXPR_OK);
    assert(strstr(err.message, "every input"));

    options.host_bindings = mismatched;
    options.host_binding_count = 2u;
    err = (cxpr_error){0};
    program = cxpr_model_compile_with_options(model, NULL, &options, &err);
    assert(!program);
    assert(err.code != CXPR_OK);
    assert(strstr(err.message, "declared input"));
    cxpr_model_free(model);
}

int main(void) {
    test_complete_artifact_descriptor_metadata();
    test_host_binding_schema_diagnostics();
    puts("C plugin artifact tests passed");
    return 0;
}
