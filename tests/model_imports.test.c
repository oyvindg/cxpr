#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_source {
    const char* id;
    const char* source;
} test_source;

typedef struct test_loader {
    const test_source* sources;
    size_t count;
} test_loader;

static char* test_strdup(const char* value) {
    size_t size = strlen(value) + 1u;
    char* copy = (char*)malloc(size);
    assert(copy);
    memcpy(copy, value, size);
    return copy;
}

static bool load_source(
    const char* importer_id,
    const char* use_path,
    void* userdata,
    char** out_id,
    char** out_source,
    cxpr_error* err) {
    const test_loader* loader = (const test_loader*)userdata;
    size_t i;
    (void)importer_id;
    (void)err;
    *out_id = NULL;
    *out_source = NULL;
    for (i = 0u; i < loader->count; ++i) {
        if (strcmp(loader->sources[i].id, use_path) == 0) {
            *out_id = test_strdup(loader->sources[i].id);
            *out_source = test_strdup(loader->sources[i].source);
            return true;
        }
    }
    return false;
}

static void test_function_only_import(void) {
    static const test_source sources[] = {
        {
            "math_helpers",
            "fn twice(x) = x * 2\n",
        },
    };
    const char root_source[] =
        "model root\n"
        "use math_helpers\n"
        "in value\n"
        "out result = math_helpers.twice(value)\n";
    test_loader loader = {sources, 1u};
    cxpr_error err = {0};
    cxpr_model* root = cxpr_model_parse(root_source, &err);
    cxpr_model_import_bundle* bundle;
    const cxpr_model_import* imports;
    cxpr_model_compiled* program;
    size_t import_count = 0u;

    assert(root);
    bundle = cxpr_model_import_bundle_build(
        "root", root, load_source, &loader, &err);
    assert(bundle);
    assert(cxpr_model_import_bundle_count(bundle) == 1u);
    assert(strcmp(cxpr_model_import_bundle_id(bundle, 0u), "math_helpers") == 0);
    imports = cxpr_model_import_bundle_root_imports(bundle, &import_count);
    assert(imports);
    assert(import_count == 1u);
    program = cxpr_model_compile_with_imports(
        root, NULL, imports, import_count, &err);
    assert(program);

    cxpr_model_compiled_free(program);
    cxpr_model_import_bundle_free(bundle);
    cxpr_model_free(root);
}

static void test_imported_function_preserves_resample(void) {
    static const test_source sources[] = {
        {"temporal", "fn hourly(x) = resample(x, every=\"1h\")\n"},
    };
    const char root_source[] =
        "model root\nuse temporal\nin close\nout result = temporal.hourly(close)\n";
    test_loader loader = {sources, 1u};
    cxpr_error err = {0};
    cxpr_model* root = cxpr_model_parse(root_source, &err);
    cxpr_model_import_bundle* bundle;
    const cxpr_model_import* imports;
    cxpr_model_compiled* program;
    size_t import_count = 0u;
    assert(root);
    bundle = cxpr_model_import_bundle_build("root", root, load_source, &loader, &err);
    if (!bundle) fprintf(stderr, "imported resample bundle failed: %s\n", err.message);
    assert(bundle);
    imports = cxpr_model_import_bundle_root_imports(bundle, &import_count);
    assert(imports && import_count == 1u);
    program = cxpr_model_compile_with_imports(root, NULL, imports, import_count, &err);
    if (!program) fprintf(stderr, "imported resample compile failed: %s\n", err.message);
    assert(program);
    cxpr_model_compiled_free(program);
    cxpr_model_import_bundle_free(bundle);
    cxpr_model_free(root);
}

static void test_cycle_is_rejected(void) {
    static const test_source sources[] = {
        {"a", "model a\nuse b\nout value = 1\n"},
        {"b", "model b\nuse a\nout value = 2\n"},
    };
    const char root_source[] = "model root\nuse a\nout value = 0\n";
    test_loader loader = {sources, 2u};
    cxpr_error err = {0};
    cxpr_model* root = cxpr_model_parse(root_source, &err);
    cxpr_model_import_bundle* bundle;

    assert(root);
    bundle = cxpr_model_import_bundle_build(
        "root", root, load_source, &loader, &err);
    assert(!bundle);
    assert(err.code == CXPR_ERR_CIRCULAR_DEPENDENCY);
    cxpr_model_free(root);
}

static void test_duplicate_namespace_is_rejected(void) {
    static const test_source sources[] = {
        {"first", "model shared\nout value = 1\n"},
        {"second", "model shared\nout value = 2\n"},
    };
    const char root_source[] =
        "model root\nuse first\nuse second\nout value = 0\n";
    test_loader loader = {sources, 2u};
    cxpr_error err = {0};
    cxpr_model* root = cxpr_model_parse(root_source, &err);
    cxpr_model_import_bundle* bundle;

    assert(root);
    bundle = cxpr_model_import_bundle_build(
        "root", root, load_source, &loader, &err);
    assert(!bundle);
    assert(err.code == CXPR_ERR_SYNTAX);
    assert(strcmp(err.message, "Duplicate model import namespace") == 0);
    cxpr_model_free(root);
}

int main(void) {
    test_function_only_import();
    test_imported_function_preserves_resample();
    test_cycle_is_rejected();
    test_duplicate_namespace_is_rejected();
    return 0;
}
