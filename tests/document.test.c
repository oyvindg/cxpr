/**
 * @file document.test.c
 * @brief Tests for generic .cxpr document manifests.
 */

#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void test_manifest_document_accepts_host_blocks_without_model(void) {
    const char* source =
        "project {\n"
        "  name = \"dynasty\"\n"
        "  language = \"c\"\n"
        "}\n"
        "vsix {\n"
        "  recommended = [\"dynasty.cxpr-tools\"]\n"
        "}\n";
    cxpr_error err = {0};
    cxpr_document* document = cxpr_parse_manifest(source, &err);
    const cxpr_model_host_block* project;
    const cxpr_model_host_block* vsix;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    assert(cxpr_document_model(document) == NULL);
    assert(cxpr_document_host_block_count(document) == 2u);

    project = cxpr_document_host_block(document, "project");
    vsix = cxpr_document_host_block(document, "vsix");
    assert(project != NULL);
    assert(vsix != NULL);
    assert(strcmp(cxpr_host_block_field_value_by_key(project, "name"), "\"dynasty\"") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(project, "language"), "\"c\"") == 0);
    assert(strcmp(cxpr_host_block_field_value_by_key(vsix, "recommended"),
                  "[\"dynasty.cxpr-tools\"]") == 0);

    cxpr_document_free(document);
    printf("  ✓ test_manifest_document_accepts_host_blocks_without_model\n");
}

static void test_manifest_document_rejects_model_syntax_without_extension(void) {
    const char* source =
        "model strategy\n"
        "in { close }\n"
        "out close\n";
    cxpr_error err = {0};
    cxpr_document* document = cxpr_parse_manifest(source, &err);

    assert(document == NULL);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(err.message != NULL);
    assert(strstr(err.message, "CXPR_DOCUMENT_EXTENSION_MODEL") != NULL);
    printf("  ✓ test_manifest_document_rejects_model_syntax_without_extension\n");
}

static void test_document_model_extension_exposes_model_view(void) {
    const char* source =
        "model strategy\n"
        "in { close }\n"
        "signal = close > 0\n"
        "out signal\n";
    cxpr_error err = {0};
    cxpr_document* document = cxpr_parse_model_document(source, &err);
    const cxpr_model* model;

    assert(document != NULL);
    assert(err.code == CXPR_OK);
    model = cxpr_document_model(document);
    assert(model != NULL);
    assert(strcmp(cxpr_model_name(model), "strategy") == 0);
    assert(cxpr_model_input_count(model) == 1u);
    assert(strcmp(cxpr_model_input(model, 0u), "close") == 0);
    assert(cxpr_model_output_count(model) == 1u);
    assert(strcmp(cxpr_model_output(model, 0u), "signal") == 0);

    cxpr_document_free(document);
    printf("  ✓ test_document_model_extension_exposes_model_view\n");
}

static void test_load_document_file_is_primary_entrypoint(void) {
    const char* path = "/tmp/cxpr_document_manifest_test.cxpr";
    FILE* file = fopen(path, "wb");
    cxpr_error err = {0};
    cxpr_document* document;
    const cxpr_model_host_block* tooling;

    assert(file != NULL);
    fputs("tooling { cli = \"dyn_cli\" }\n", file);
    fclose(file);

    document = cxpr_load_manifest_file(path, &err);
    assert(document != NULL);
    assert(err.code == CXPR_OK);
    tooling = cxpr_document_host_block(document, "tooling");
    assert(tooling != NULL);
    assert(strcmp(cxpr_host_block_field_value_by_key(tooling, "cli"), "\"dyn_cli\"") == 0);

    cxpr_document_free(document);
    unlink(path);
    printf("  ✓ test_load_document_file_is_primary_entrypoint\n");
}

int main(void) {
    printf("Running cxpr document tests...\n");
    test_manifest_document_accepts_host_blocks_without_model();
    test_manifest_document_rejects_model_syntax_without_extension();
    test_document_model_extension_exposes_model_view();
    test_load_document_file_is_primary_entrypoint();
    printf("All document tests passed.\n");
    return 0;
}
