#include <cxpr/cxpr.h>
#include <cxpr/debug_map.h>
#include <cxpr/plugins/debug_map.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_debug_map_metadata_contract(void) {
    static const char source[] =
        "model debug_test\n"
        "in { close, enabled }\n"
        "$threshold = 3\n"
        "above = enabled and close > $threshold\n"
        "scaled = close * 2\n"
        "out { above, scaled }\n";
    const cxpr_model_compile_options compile_options = {
        CXPR_MODEL_BACKEND_C, true, false
    };
    const size_t selected[] = {0u};
    const cxpr_debug_map_plugin_options options = {
        "test_debug_map", "const", selected, 1u
    };
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_compiled* compiled;
    char* first;
    char* second;

    assert(model != NULL);
    compiled = cxpr_model_compile_with_options(
        model, NULL, &compile_options, &err);
    assert(compiled != NULL);
    first = cxpr_debug_map_plugin_source_from_model(
        model, compiled, "fixtures/debug.cxpr", &options, &err);
    second = cxpr_debug_map_plugin_source_from_model(
        model, compiled, "fixtures/debug.cxpr", &options, &err);
    assert(first != NULL);
    assert(second != NULL);
    assert(strcmp(first, second) == 0);
    assert(strstr(first, "CXPR_DEBUG_MAP_ABI_VERSION") != NULL);
    assert(strstr(first, ".source_path = \"fixtures/debug.cxpr\"") != NULL);
    assert(strstr(first, ".canonical_source = \"enabled and close > $threshold\"") != NULL);
    assert(strstr(first, ".result_type = CXPR_DEBUG_RESULT_") != NULL);
    assert(strstr(first, "_dependencies[]") != NULL);
    assert(strstr(first, ".dependencies = NULL") != NULL);
    assert(strstr(first, ".output_count = 1u") != NULL);
    assert(strstr(first, ".name = \"above\"") != NULL);
    assert(strstr(first, ".name = \"scaled\"") != NULL); /* internal node */

    cxpr_debug_map_plugin_source_free(first);
    cxpr_debug_map_plugin_source_free(second);
    cxpr_model_compiled_free(compiled);
    cxpr_model_free(model);
}

static void test_debug_map_validation(void) {
    static const cxpr_debug_node_id dependencies[] = {11u};
    static const cxpr_debug_node nodes[] = {
        {
            11u, "input", CXPR_DEBUG_NODE_INPUT, CXPR_DEBUG_RESULT_NUMBER,
            NULL, {0}, 0, "input", NULL, 0u, CXPR_DEBUG_TRACE_SLOT_NONE
        },
        {
            12u, "value", CXPR_DEBUG_NODE_EXPRESSION,
            CXPR_DEBUG_RESULT_NUMBER, NULL, {0}, 0, "input * 2",
            dependencies, 1u, CXPR_DEBUG_TRACE_SLOT_NONE
        }
    };
    static const cxpr_debug_output outputs[] = {
        {21u, "value", 12u, CXPR_DEBUG_RESULT_NUMBER}
    };
    static const cxpr_debug_map map = {
        CXPR_DEBUG_MAP_ABI_VERSION, "validation", nodes, 2u, outputs, 1u
    };
    assert(cxpr_debug_map_validate(&map));
}

int main(void) {
    test_debug_map_metadata_contract();
    test_debug_map_validation();
    puts("Debug map plugin tests passed");
    return 0;
}
