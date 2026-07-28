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
    test_artifact_sink* sink = (test_artifact_sink*)user;
    (void)err;
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
        "model graph_demo\n"
        "in source\n"
        "$period = 9 { description = \"Period\" }\n"
        "state {\n"
        "    initialized = 0\n"
        "    acc = 0\n"
        "}\n"
        "value = initialized > 0 ? acc + source / $period : source\n"
        "initialized := 1\n"
        "acc := value\n"
        "out value {\n"
        "    label = \"Value\"\n"
        "    plot { key = \"value\" }\n"
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
    char* graph;

    assert(model != NULL);
    assert(cxpr_model_validate(model, &err));

    graph = cxpr_graph_plugin_graph_from_model(model, NULL, &err);
    assert(graph != NULL);
    assert(strstr(graph, "\"schema\":\"cxpr.graph.v1\"") != NULL);
    assert(strstr(graph, "\"id\":\"model:graph_demo\"") != NULL);
    assert(strstr(graph, "\"id\":\"input:source\"") != NULL);
    assert(strstr(graph, "\"id\":\"param:period\"") != NULL);
    assert(strstr(graph, "\"id\":\"state:acc\"") != NULL);
    assert(strstr(graph, "\"id\":\"binding:value\"") != NULL);
    assert(strstr(graph, "\"id\":\"state_update:acc:4\"") != NULL);
    assert(strstr(graph, "\"id\":\"output:value\"") != NULL);
    assert(strstr(graph, "\"kind\":\"depends_on\"") != NULL);
    assert(strstr(graph, "\"kind\":\"commits\"") != NULL);
    assert(strstr(graph, "\"kind\":\"metadata\"") != NULL);
    assert(strstr(graph, "plot { key = \\\"value\\\" }") != NULL);
    cxpr_graph_plugin_graph_free(graph);

    event.model_path = "graph_demo.cxpr";
    event.model = model;
    event.program = NULL;
    assert(cxpr_graph_plugin_emit_graph(&event, NULL, &host, &err));
    assert(strcmp(sink.kind, "cxpr.graph.v1") == 0);
    assert(sink.data != NULL);
    assert(strstr(sink.data, "\"edges\":[") != NULL);

    free(sink.data);
    cxpr_model_free(model);
    return 0;
}
