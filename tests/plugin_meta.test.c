#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_artifact_sink {
    char* data;
    size_t len;
    size_t cap;
    const char* kind;
} test_artifact_sink;

static int test_begin_artifact(void* user, const cxpr_model_plugin_artifact_event* event, cxpr_error* err) {
    (void)err;
    test_artifact_sink* sink = (test_artifact_sink*)user;
    sink->kind = event->kind;
    return 1;
}

static int test_write_artifact(void* user, const void* data, size_t size, cxpr_error* err) {
    test_artifact_sink* sink = (test_artifact_sink*)user;
    const char* bytes = (const char*)data;
    char* next;
    (void)err;
    if (sink->len + size + 1u > sink->cap) {
        size_t cap = sink->cap ? sink->cap : 512u;
        while (sink->len + size + 1u > cap) cap *= 2u;
        next = (char*)realloc(sink->data, cap);
        if (!next) return 0;
        sink->data = next;
        sink->cap = cap;
    }
    memcpy(sink->data + sink->len, bytes, size);
    sink->len += size;
    sink->data[sink->len] = '\0';
    return 1;
}

static int test_end_artifact(void* user, cxpr_error* err) {
    (void)user;
    (void)err;
    return 1;
}

int main(void) {
    const char* source =
        "model demo {\n"
        "    label = \"Demo\"\n"
        "}\n"
        "use math\n"
        "in source\n"
        "$period = 14 { description = \"Period\" }\n"
        "value = source\n"
        "out value {\n"
        "    label = \"Value\"\n"
        "    plot {\n"
        "        key = \"demo_value\"\n"
        "        pane = \"main\"\n"
        "        style = line\n"
        "    }\n"
        "}\n";
    cxpr_error err = {0};
    cxpr_model* model = cxpr_model_parse(source, &err);
    cxpr_model_plugin_event event;
    test_artifact_sink sink = {0};
    cxpr_model_plugin_host host = {
        &sink,
        test_begin_artifact,
        test_write_artifact,
        test_end_artifact
    };
    char* manifest;

    assert(model != NULL);
    assert(cxpr_model_validate(model, &err));

    manifest = cxpr_meta_plugin_manifest_from_model(model, NULL, &err);
    assert(manifest != NULL);
    assert(strstr(manifest, "\"schema\":\"cxpr.meta.manifest.v1\"") != NULL);
    assert(strstr(manifest, "\"name\":\"demo\"") != NULL);
    assert(strstr(manifest, "\"uses\":[\"math\"]") != NULL);
    assert(strstr(manifest, "\"inputs\":[\"source\"]") != NULL);
    assert(strstr(manifest, "\"name\":\"period\",\"defaultExpr\":\"14\"") != NULL);
    assert(strstr(manifest, "\"outputs\":[{\"name\":\"value\",\"metadata\"") != NULL);
    assert(strstr(manifest, "\"targetKind\":\"output\"") != NULL);
    assert(strstr(manifest, "plot {") != NULL);
    assert(strstr(manifest, "demo_value") != NULL);
    cxpr_meta_plugin_manifest_free(manifest);

    event.model_path = "demo.cxpr";
    event.model = model;
    event.compiled = NULL;
    assert(cxpr_meta_plugin_emit_manifest(&event, NULL, &host, &err));
    assert(strcmp(sink.kind, "cxpr.meta.manifest.v1") == 0);
    assert(sink.data != NULL);
    assert(strstr(sink.data, "\"metadata\":[") != NULL);

    free(sink.data);
    cxpr_model_free(model);
    return 0;
}
