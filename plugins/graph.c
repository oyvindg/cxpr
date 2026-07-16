#include <cxpr/plugins/graph.h>
#include <cxpr/model/model.h>

#include <stdlib.h>
#include <string.h>

typedef struct cxpr_graph_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_graph_buf;

static void cxpr_graph_set_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0u;
    err->line = 0u;
    err->column = 0u;
}

static int cxpr_graph_append_bytes(cxpr_graph_buf* b, const char* text, size_t n) {
    if (!b || !text) return 0;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 2048u;
        char* next;
        while (b->len + n + 1u > cap) cap *= 2u;
        next = (char*)realloc(b->data, cap);
        if (!next) return 0;
        b->data = next;
        b->cap = cap;
    }
    memcpy(b->data + b->len, text, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int cxpr_graph_append(cxpr_graph_buf* b, const char* text) {
    return cxpr_graph_append_bytes(b, text, strlen(text));
}

static int cxpr_graph_append_json_string(cxpr_graph_buf* b, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");
    if (!cxpr_graph_append(b, "\"")) return 0;
    while (*p) {
        char escaped[7];
        switch (*p) {
            case '\"': if (!cxpr_graph_append(b, "\\\"")) return 0; break;
            case '\\': if (!cxpr_graph_append(b, "\\\\")) return 0; break;
            case '\b': if (!cxpr_graph_append(b, "\\b")) return 0; break;
            case '\f': if (!cxpr_graph_append(b, "\\f")) return 0; break;
            case '\n': if (!cxpr_graph_append(b, "\\n")) return 0; break;
            case '\r': if (!cxpr_graph_append(b, "\\r")) return 0; break;
            case '\t': if (!cxpr_graph_append(b, "\\t")) return 0; break;
            default:
                if (*p < 0x20u) {
                    static const char hex[] = "0123456789abcdef";
                    escaped[0] = '\\';
                    escaped[1] = 'u';
                    escaped[2] = '0';
                    escaped[3] = '0';
                    escaped[4] = hex[*p >> 4u];
                    escaped[5] = hex[*p & 0x0fu];
                    escaped[6] = '\0';
                    if (!cxpr_graph_append(b, escaped)) return 0;
                } else if (!cxpr_graph_append_bytes(b, (const char*)p, 1u)) {
                    return 0;
                }
                break;
        }
        ++p;
    }
    return cxpr_graph_append(b, "\"");
}

static const char* cxpr_graph_binding_kind_name(cxpr_model_binding_kind kind) {
    switch (kind) {
        case CXPR_MODEL_BINDING_EXPR: return "binding";
        case CXPR_MODEL_BINDING_STATE: return "state";
        case CXPR_MODEL_BINDING_STATE_UPDATE: return "state_update";
        default: return "binding";
    }
}

static const char* cxpr_graph_metadata_target_kind_name(
    cxpr_model_metadata_target_kind kind) {
    switch (kind) {
        case CXPR_MODEL_METADATA_TARGET_MODEL: return "model";
        case CXPR_MODEL_METADATA_TARGET_USE: return "use";
        case CXPR_MODEL_METADATA_TARGET_INPUT: return "input";
        case CXPR_MODEL_METADATA_TARGET_PARAM: return "param";
        case CXPR_MODEL_METADATA_TARGET_BINDING: return "binding";
        case CXPR_MODEL_METADATA_TARGET_STATE: return "state";
        case CXPR_MODEL_METADATA_TARGET_FUNCTION: return "function";
        case CXPR_MODEL_METADATA_TARGET_OUTPUT: return "output";
        default: return "unknown";
    }
}

static int cxpr_graph_append_id(
    cxpr_graph_buf* out,
    const char* kind,
    const char* name,
    size_t index,
    int use_index) {
    if (!cxpr_graph_append(out, "\"") ||
        !cxpr_graph_append(out, kind) ||
        !cxpr_graph_append(out, ":")) {
        return 0;
    }
    if (name) {
        const unsigned char* p = (const unsigned char*)name;
        while (*p) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.') {
                if (!cxpr_graph_append_bytes(out, (const char*)p, 1u)) return 0;
            } else if (!cxpr_graph_append(out, "_")) {
                return 0;
            }
            ++p;
        }
    }
    if (use_index) {
        char buf[32];
        snprintf(buf, sizeof(buf), ":%zu", index);
        if (!cxpr_graph_append(out, buf)) return 0;
    }
    return cxpr_graph_append(out, "\"");
}

static int cxpr_graph_append_node_start(
    cxpr_graph_buf* out,
    int* first,
    const char* id_kind,
    const char* name,
    size_t index,
    int use_index,
    const char* kind) {
    if (!*first && !cxpr_graph_append(out, ",")) return 0;
    *first = 0;
    return cxpr_graph_append(out, "{\"id\":") &&
           cxpr_graph_append_id(out, id_kind, name, index, use_index) &&
           cxpr_graph_append(out, ",\"kind\":") &&
           cxpr_graph_append_json_string(out, kind) &&
           cxpr_graph_append(out, ",\"label\":") &&
           cxpr_graph_append_json_string(out, name ? name : "");
}

static int cxpr_graph_append_edge(
    cxpr_graph_buf* out,
    int* first,
    const char* source_kind,
    const char* source_name,
    size_t source_index,
    int source_use_index,
    const char* target_kind,
    const char* target_name,
    size_t target_index,
    int target_use_index,
    const char* kind) {
    if (!*first && !cxpr_graph_append(out, ",")) return 0;
    *first = 0;
    return cxpr_graph_append(out, "{\"source\":") &&
           cxpr_graph_append_id(out, source_kind, source_name, source_index, source_use_index) &&
           cxpr_graph_append(out, ",\"target\":") &&
           cxpr_graph_append_id(out, target_kind, target_name, target_index, target_use_index) &&
           cxpr_graph_append(out, ",\"kind\":") &&
           cxpr_graph_append_json_string(out, kind) &&
           cxpr_graph_append(out, "}");
}

static int cxpr_graph_has_input(const cxpr_model* model, const char* name) {
    for (size_t i = 0u; i < cxpr_model_input_count(model); ++i) {
        if (strcmp(cxpr_model_input(model, i), name) == 0) return 1;
    }
    return 0;
}

static int cxpr_graph_has_param(const cxpr_model* model, const char* name) {
    for (size_t i = 0u; i < cxpr_model_constant_count(model); ++i) {
        if (strcmp(cxpr_model_constant_name(model, i), name) == 0) return 1;
    }
    return 0;
}

static int cxpr_graph_has_state(const cxpr_model* model, const char* name) {
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        if (cxpr_model_binding_kind_at(model, i) == CXPR_MODEL_BINDING_STATE &&
            strcmp(cxpr_model_binding_name(model, i), name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int cxpr_graph_has_expr_binding(const cxpr_model* model, const char* name) {
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        if (cxpr_model_binding_kind_at(model, i) == CXPR_MODEL_BINDING_EXPR &&
            strcmp(cxpr_model_binding_name(model, i), name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void cxpr_graph_resolve_ref(
    const cxpr_model* model,
    const char* ref,
    const char** out_kind,
    const char** out_name) {
    if (cxpr_graph_has_input(model, ref)) {
        *out_kind = "input";
    } else if (cxpr_graph_has_param(model, ref)) {
        *out_kind = "param";
    } else if (cxpr_graph_has_expr_binding(model, ref)) {
        *out_kind = "binding";
    } else if (cxpr_graph_has_state(model, ref)) {
        *out_kind = "state";
    } else {
        *out_kind = "external";
    }
    *out_name = ref;
}

static int cxpr_graph_append_binding_node(
    cxpr_graph_buf* out,
    int* first,
    const cxpr_model* model,
    size_t index,
    int include_expression_source) {
    const cxpr_model_binding_kind binding_kind = cxpr_model_binding_kind_at(model, index);
    const char* name = cxpr_model_binding_name(model, index);
    const char* kind = cxpr_graph_binding_kind_name(binding_kind);
    const int use_index = binding_kind == CXPR_MODEL_BINDING_STATE_UPDATE;
    char* expr = NULL;
    int ok;

    ok = cxpr_graph_append_node_start(out, first, kind, name, index, use_index, kind);
    if (!ok) return 0;
    if (include_expression_source) {
        expr = cxpr_ast_to_string(cxpr_model_binding_expr(model, index));
        if (!expr) return 0;
        ok = cxpr_graph_append(out, ",\"expr\":") &&
             cxpr_graph_append_json_string(out, expr);
        free(expr);
        if (!ok) return 0;
    }
    return cxpr_graph_append(out, "}");
}

static int cxpr_graph_append_nodes(
    cxpr_graph_buf* out,
    const cxpr_model* model,
    const cxpr_graph_plugin_options* options) {
    int first = 1;
    if (!cxpr_graph_append(out, "\"nodes\":[")) return 0;
    if (!cxpr_graph_append_node_start(
            out, &first, "model", cxpr_model_name(model), 0u, 0, "model") ||
        !cxpr_graph_append(out, "}")) {
        return 0;
    }
    for (size_t i = 0u; i < cxpr_model_use_count(model); ++i) {
        const char* name = cxpr_model_use(model, i);
        if (!cxpr_graph_append_node_start(out, &first, "use", name, i, 0, "use") ||
            !cxpr_graph_append(out, "}")) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_input_count(model); ++i) {
        const char* name = cxpr_model_input(model, i);
        if (!cxpr_graph_append_node_start(out, &first, "input", name, i, 0, "input") ||
            !cxpr_graph_append(out, "}")) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_constant_count(model); ++i) {
        const char* name = cxpr_model_constant_name(model, i);
        char* expr = NULL;
        if (!cxpr_graph_append_node_start(out, &first, "param", name, i, 0, "param")) {
            return 0;
        }
        if (options->include_expression_source) {
            expr = cxpr_ast_to_string(cxpr_model_constant_expr(model, i));
            if (!expr) return 0;
            if (!cxpr_graph_append(out, ",\"expr\":") ||
                !cxpr_graph_append_json_string(out, expr)) {
                free(expr);
                return 0;
            }
            free(expr);
        }
        if (!cxpr_graph_append(out, "}")) return 0;
    }
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        if (!cxpr_graph_append_binding_node(out, &first, model, i,
                                           options->include_expression_source)) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_output_count(model); ++i) {
        const char* name = cxpr_model_output(model, i);
        if (!cxpr_graph_append_node_start(out, &first, "output", name, i, 0, "output") ||
            !cxpr_graph_append(out, "}")) {
            return 0;
        }
    }
    if (options->include_metadata) {
        for (size_t i = 0u; i < cxpr_model_metadata_count(model); ++i) {
            const char* label = cxpr_model_metadata_target_name(model, i);
            if (!cxpr_graph_append_node_start(out, &first, "metadata", label, i, 1, "metadata") ||
                !cxpr_graph_append(out, ",\"metadataName\":") ||
                !cxpr_graph_append_json_string(out, cxpr_model_metadata_name(model, i)) ||
                !cxpr_graph_append(out, ",\"targetKind\":") ||
                !cxpr_graph_append_json_string(out, cxpr_graph_metadata_target_kind_name(
                    cxpr_model_metadata_target_kind_at(model, i))) ||
                !cxpr_graph_append(out, ",\"targetName\":") ||
                !cxpr_graph_append_json_string(out, label) ||
                !cxpr_graph_append(out, ",\"body\":") ||
                !cxpr_graph_append_json_string(out, cxpr_model_metadata_body(model, i)) ||
                !cxpr_graph_append(out, "}")) {
                return 0;
            }
        }
    }
    return cxpr_graph_append(out, "]");
}

static int cxpr_graph_append_expr_ref_edges(
    cxpr_graph_buf* out,
    int* first,
    const cxpr_model* model,
    const cxpr_ast* expr,
    const char* target_kind,
    const char* target_name,
    size_t target_index,
    int target_use_index) {
    size_t ref_count = cxpr_ast_references(expr, NULL, 0u);
    const char** refs;
    if (ref_count == 0u) return 1;
    refs = (const char**)calloc(ref_count, sizeof(char*));
    if (!refs) return 0;
    ref_count = cxpr_ast_references(expr, refs, ref_count);
    for (size_t i = 0u; i < ref_count; ++i) {
        const char* source_kind;
        const char* source_name;
        cxpr_graph_resolve_ref(model, refs[i], &source_kind, &source_name);
        if (!cxpr_graph_append_edge(out, first,
                                    source_kind, source_name, 0u, 0,
                                    target_kind, target_name, target_index, target_use_index,
                                    "depends_on")) {
            free(refs);
            return 0;
        }
    }
    free(refs);
    return 1;
}

static void cxpr_graph_output_source(
    const cxpr_model* model,
    const char* output,
    const char** out_kind,
    const char** out_name) {
    if (cxpr_graph_has_expr_binding(model, output)) {
        *out_kind = "binding";
    } else if (cxpr_graph_has_state(model, output)) {
        *out_kind = "state";
    } else if (cxpr_graph_has_input(model, output)) {
        *out_kind = "input";
    } else {
        *out_kind = "external";
    }
    *out_name = output;
}

static int cxpr_graph_append_metadata_edge(
    cxpr_graph_buf* out,
    int* first,
    const cxpr_model* model,
    size_t metadata_index) {
    const cxpr_model_metadata_target_kind target_kind =
        cxpr_model_metadata_target_kind_at(model, metadata_index);
    const char* target_name = cxpr_model_metadata_target_name(model, metadata_index);
    const char* source_kind = "model";
    const char* source_name = cxpr_model_name(model);

    switch (target_kind) {
        case CXPR_MODEL_METADATA_TARGET_MODEL:
            source_kind = "model";
            source_name = cxpr_model_name(model);
            break;
        case CXPR_MODEL_METADATA_TARGET_USE:
            source_kind = "use";
            source_name = target_name;
            break;
        case CXPR_MODEL_METADATA_TARGET_INPUT:
            source_kind = "input";
            source_name = target_name;
            break;
        case CXPR_MODEL_METADATA_TARGET_PARAM:
            source_kind = "param";
            source_name = target_name;
            break;
        case CXPR_MODEL_METADATA_TARGET_STATE:
            source_kind = "state";
            source_name = target_name;
            break;
        case CXPR_MODEL_METADATA_TARGET_OUTPUT:
            source_kind = "output";
            source_name = target_name;
            break;
        case CXPR_MODEL_METADATA_TARGET_BINDING:
        case CXPR_MODEL_METADATA_TARGET_FUNCTION:
        default:
            source_kind = "binding";
            source_name = target_name;
            break;
    }
    return cxpr_graph_append_edge(out, first,
                                  source_kind, source_name, 0u, 0,
                                  "metadata", target_name, metadata_index, 1,
                                  "metadata");
}

static int cxpr_graph_append_edges(
    cxpr_graph_buf* out,
    const cxpr_model* model,
    const cxpr_graph_plugin_options* options) {
    int first = 1;
    if (!cxpr_graph_append(out, "\"edges\":[")) return 0;
    for (size_t i = 0u; i < cxpr_model_use_count(model); ++i) {
        if (!cxpr_graph_append_edge(out, &first,
                                    "model", cxpr_model_name(model), 0u, 0,
                                    "use", cxpr_model_use(model, i), i, 0,
                                    "uses")) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_input_count(model); ++i) {
        if (!cxpr_graph_append_edge(out, &first,
                                    "model", cxpr_model_name(model), 0u, 0,
                                    "input", cxpr_model_input(model, i), i, 0,
                                    "declares")) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_constant_count(model); ++i) {
        if (!cxpr_graph_append_edge(out, &first,
                                    "model", cxpr_model_name(model), 0u, 0,
                                    "param", cxpr_model_constant_name(model, i), i, 0,
                                    "declares") ||
            !cxpr_graph_append_expr_ref_edges(out, &first, model,
                                              cxpr_model_constant_expr(model, i),
                                              "param", cxpr_model_constant_name(model, i),
                                              i, 0)) {
            return 0;
        }
    }
    for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
        const cxpr_model_binding_kind binding_kind = cxpr_model_binding_kind_at(model, i);
        const char* kind = cxpr_graph_binding_kind_name(binding_kind);
        const char* name = cxpr_model_binding_name(model, i);
        const int use_index = binding_kind == CXPR_MODEL_BINDING_STATE_UPDATE;
        if (!cxpr_graph_append_edge(out, &first,
                                    "model", cxpr_model_name(model), 0u, 0,
                                    kind, name, i, use_index,
                                    "declares") ||
            !cxpr_graph_append_expr_ref_edges(out, &first, model,
                                              cxpr_model_binding_expr(model, i),
                                              kind, name, i, use_index)) {
            return 0;
        }
        if (binding_kind == CXPR_MODEL_BINDING_STATE_UPDATE) {
            if (!cxpr_graph_append_edge(out, &first,
                                        "state_update", name, i, 1,
                                        "state", name, 0u, 0,
                                        "commits")) {
                return 0;
            }
        }
    }
    for (size_t i = 0u; i < cxpr_model_output_count(model); ++i) {
        const char* output = cxpr_model_output(model, i);
        const char* source_kind;
        const char* source_name;
        cxpr_graph_output_source(model, output, &source_kind, &source_name);
        if (!cxpr_graph_append_edge(out, &first,
                                    "model", cxpr_model_name(model), 0u, 0,
                                    "output", output, i, 0,
                                    "publishes") ||
            !cxpr_graph_append_edge(out, &first,
                                    source_kind, source_name, 0u, 0,
                                    "output", output, i, 0,
                                    "feeds")) {
            return 0;
        }
    }
    if (options->include_metadata) {
        for (size_t i = 0u; i < cxpr_model_metadata_count(model); ++i) {
            if (!cxpr_graph_append_metadata_edge(out, &first, model, i)) {
                return 0;
            }
        }
    }
    return cxpr_graph_append(out, "]");
}

char* cxpr_graph_plugin_graph_from_model(
    const cxpr_model* model,
    const cxpr_graph_plugin_options* options,
    cxpr_error* err) {
    static const cxpr_graph_plugin_options defaults = {1, 1};
    const cxpr_graph_plugin_options* opts = options ? options : &defaults;
    cxpr_graph_buf out = {0};

    if (!model) {
        cxpr_graph_set_error(err, CXPR_ERR_SYNTAX, "cxpr graph plugin requires a model");
        return NULL;
    }
    if (!cxpr_graph_append(&out, "{\"schema\":\"cxpr.graph.v1\",\"model\":") ||
        !cxpr_graph_append_json_string(&out, cxpr_model_name(model)) ||
        !cxpr_graph_append(&out, ",")) {
        free(out.data);
        cxpr_graph_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }
    if (!cxpr_graph_append_nodes(&out, model, opts) ||
        !cxpr_graph_append(&out, ",") ||
        !cxpr_graph_append_edges(&out, model, opts) ||
        !cxpr_graph_append(&out, "}")) {
        free(out.data);
        cxpr_graph_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }
    return out.data;
}

int cxpr_graph_plugin_emit_graph(
    const cxpr_plugin_model_event* event,
    const cxpr_graph_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    cxpr_plugin_artifact_event artifact = {
        "cxpr_graph",
        "cxpr.graph.v1",
        NULL
    };
    char* graph;
    int ok;

    if (!event || !event->model || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        cxpr_graph_set_error(err, CXPR_ERR_SYNTAX, "cxpr graph plugin requires model and host callbacks");
        return 0;
    }
    graph = cxpr_graph_plugin_graph_from_model(event->model, options, err);
    if (!graph) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, graph, strlen(graph), err) &&
         host->end_artifact(host->user, err);
    free(graph);
    return ok ? 1 : 0;
}

void cxpr_graph_plugin_graph_free(char* graph) {
    free(graph);
}
