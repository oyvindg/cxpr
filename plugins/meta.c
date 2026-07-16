#include <cxpr/plugins/meta.h>
#include <cxpr/model/model.h>

#include <stdlib.h>
#include <string.h>

typedef struct cxpr_meta_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_meta_buf;

static void cxpr_meta_set_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0u;
    err->line = 0u;
    err->column = 0u;
}

static int cxpr_meta_append_bytes(cxpr_meta_buf* b, const char* text, size_t n) {
    if (!b || !text) return 0;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 1024u;
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

static int cxpr_meta_append(cxpr_meta_buf* b, const char* text) {
    return cxpr_meta_append_bytes(b, text, strlen(text));
}

static int cxpr_meta_append_json_string(cxpr_meta_buf* b, const char* text) {
    const unsigned char* p = (const unsigned char*)(text ? text : "");
    if (!cxpr_meta_append(b, "\"")) return 0;
    while (*p) {
        char escaped[7];
        switch (*p) {
            case '\"':
                if (!cxpr_meta_append(b, "\\\"")) return 0;
                break;
            case '\\':
                if (!cxpr_meta_append(b, "\\\\")) return 0;
                break;
            case '\b':
                if (!cxpr_meta_append(b, "\\b")) return 0;
                break;
            case '\f':
                if (!cxpr_meta_append(b, "\\f")) return 0;
                break;
            case '\n':
                if (!cxpr_meta_append(b, "\\n")) return 0;
                break;
            case '\r':
                if (!cxpr_meta_append(b, "\\r")) return 0;
                break;
            case '\t':
                if (!cxpr_meta_append(b, "\\t")) return 0;
                break;
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
                    if (!cxpr_meta_append(b, escaped)) return 0;
                } else if (!cxpr_meta_append_bytes(b, (const char*)p, 1u)) {
                    return 0;
                }
                break;
        }
        ++p;
    }
    return cxpr_meta_append(b, "\"");
}

static const char* cxpr_meta_metadata_target_kind_name(
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

static const char* cxpr_meta_binding_kind_name(cxpr_model_binding_kind kind) {
    switch (kind) {
        case CXPR_MODEL_BINDING_EXPR: return "expr";
        case CXPR_MODEL_BINDING_STATE: return "state";
        case CXPR_MODEL_BINDING_STATE_UPDATE: return "state_update";
        default: return "unknown";
    }
}

static int cxpr_meta_append_name_array(
    cxpr_meta_buf* out,
    const cxpr_model* model,
    size_t count,
    const char* (*name_at)(const cxpr_model*, size_t)) {
    if (!cxpr_meta_append(out, "[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        if ((i > 0u && !cxpr_meta_append(out, ",")) ||
            !cxpr_meta_append_json_string(out, name_at(model, i))) {
            return 0;
        }
    }
    return cxpr_meta_append(out, "]");
}

static int cxpr_meta_append_constants(cxpr_meta_buf* out, const cxpr_model* model) {
    const size_t count = cxpr_model_constant_count(model);
    if (!cxpr_meta_append(out, "\"params\":[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        char* expr = cxpr_ast_to_string(cxpr_model_constant_expr(model, i));
        int ok = 1;
        if (!expr) return 0;
        ok = (i == 0u || cxpr_meta_append(out, ",")) &&
             cxpr_meta_append(out, "{\"name\":") &&
             cxpr_meta_append_json_string(out, cxpr_model_constant_name(model, i)) &&
             cxpr_meta_append(out, ",\"defaultExpr\":") &&
             cxpr_meta_append_json_string(out, expr) &&
             cxpr_meta_append(out, "}");
        free(expr);
        if (!ok) return 0;
    }
    return cxpr_meta_append(out, "]");
}

static int cxpr_meta_append_bindings(cxpr_meta_buf* out, const cxpr_model* model) {
    const size_t count = cxpr_model_binding_count(model);
    if (!cxpr_meta_append(out, "\"bindings\":[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        char* expr = cxpr_ast_to_string(cxpr_model_binding_expr(model, i));
        int ok = 1;
        if (!expr) return 0;
        ok = (i == 0u || cxpr_meta_append(out, ",")) &&
             cxpr_meta_append(out, "{\"name\":") &&
             cxpr_meta_append_json_string(out, cxpr_model_binding_name(model, i)) &&
             cxpr_meta_append(out, ",\"kind\":") &&
             cxpr_meta_append_json_string(out,
                 cxpr_meta_binding_kind_name(cxpr_model_binding_kind_at(model, i))) &&
             cxpr_meta_append(out, ",\"expr\":") &&
             cxpr_meta_append_json_string(out, expr) &&
             cxpr_meta_append(out, "}");
        free(expr);
        if (!ok) return 0;
    }
    return cxpr_meta_append(out, "]");
}

static int cxpr_meta_append_metadata_ref(
    cxpr_meta_buf* out,
    const cxpr_model* model,
    size_t index) {
    return cxpr_meta_append(out, "{\"name\":") &&
           cxpr_meta_append_json_string(out, cxpr_model_metadata_name(model, index)) &&
           cxpr_meta_append(out, ",\"targetKind\":") &&
           cxpr_meta_append_json_string(out,
               cxpr_meta_metadata_target_kind_name(
                   cxpr_model_metadata_target_kind_at(model, index))) &&
           cxpr_meta_append(out, ",\"targetName\":") &&
           cxpr_meta_append_json_string(out, cxpr_model_metadata_target_name(model, index)) &&
           cxpr_meta_append(out, ",\"body\":") &&
           cxpr_meta_append_json_string(out, cxpr_model_metadata_body(model, index)) &&
           cxpr_meta_append(out, "}");
}

static int cxpr_meta_append_output_metadata(
    cxpr_meta_buf* out,
    const cxpr_model* model,
    const char* output_name) {
    const size_t count = cxpr_model_metadata_count(model);
    size_t written = 0u;
    if (!cxpr_meta_append(out, "\"metadata\":[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_model_metadata_target_kind_at(model, i) !=
                CXPR_MODEL_METADATA_TARGET_OUTPUT ||
            strcmp(cxpr_model_metadata_target_name(model, i), output_name) != 0) {
            continue;
        }
        if ((written > 0u && !cxpr_meta_append(out, ",")) ||
            !cxpr_meta_append_metadata_ref(out, model, i)) {
            return 0;
        }
        ++written;
    }
    return cxpr_meta_append(out, "]");
}

static int cxpr_meta_append_outputs(
    cxpr_meta_buf* out,
    const cxpr_model* model,
    int include_metadata) {
    const size_t count = cxpr_model_output_count(model);
    if (!cxpr_meta_append(out, "\"outputs\":[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        const char* name = cxpr_model_output(model, i);
        if ((i > 0u && !cxpr_meta_append(out, ",")) ||
            !cxpr_meta_append(out, "{\"name\":") ||
            !cxpr_meta_append_json_string(out, name)) {
            return 0;
        }
        if (include_metadata) {
            if (!cxpr_meta_append(out, ",") ||
                !cxpr_meta_append_output_metadata(out, model, name)) {
                return 0;
            }
        }
        if (!cxpr_meta_append(out, "}")) return 0;
    }
    return cxpr_meta_append(out, "]");
}

static int cxpr_meta_append_metadata(cxpr_meta_buf* out, const cxpr_model* model) {
    const size_t count = cxpr_model_metadata_count(model);
    if (!cxpr_meta_append(out, "\"metadata\":[")) return 0;
    for (size_t i = 0u; i < count; ++i) {
        if ((i > 0u && !cxpr_meta_append(out, ",")) ||
            !cxpr_meta_append_metadata_ref(out, model, i)) {
            return 0;
        }
    }
    return cxpr_meta_append(out, "]");
}

char* cxpr_meta_plugin_manifest_from_model(
    const cxpr_model* model,
    const cxpr_meta_plugin_options* options,
    cxpr_error* err) {
    static const cxpr_meta_plugin_options defaults = {1, 1};
    const cxpr_meta_plugin_options* opts = options ? options : &defaults;
    cxpr_meta_buf out = {0};

    if (!model) {
        cxpr_meta_set_error(err, CXPR_ERR_SYNTAX, "cxpr meta plugin requires a model");
        return NULL;
    }

    if (!cxpr_meta_append(&out, "{\"schema\":\"cxpr.meta.manifest.v1\",\"name\":") ||
        !cxpr_meta_append_json_string(&out, cxpr_model_name(model))) {
        free(out.data);
        cxpr_meta_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }

    if (opts->include_declarations) {
        if (!cxpr_meta_append(&out, ",\"uses\":") ||
            !cxpr_meta_append_name_array(
                &out, model, cxpr_model_use_count(model), cxpr_model_use) ||
            !cxpr_meta_append(&out, ",\"inputs\":") ||
            !cxpr_meta_append_name_array(
                &out, model, cxpr_model_input_count(model), cxpr_model_input) ||
            !cxpr_meta_append(&out, ",") ||
            !cxpr_meta_append_constants(&out, model) ||
            !cxpr_meta_append(&out, ",") ||
            !cxpr_meta_append_bindings(&out, model)) {
            free(out.data);
            cxpr_meta_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
            return NULL;
        }
    }

    if (!cxpr_meta_append(&out, ",") ||
        !cxpr_meta_append_outputs(&out, model, opts->include_metadata)) {
        free(out.data);
        cxpr_meta_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }

    if (opts->include_metadata) {
        if (!cxpr_meta_append(&out, ",") ||
            !cxpr_meta_append_metadata(&out, model)) {
            free(out.data);
            cxpr_meta_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
            return NULL;
        }
    }

    if (!cxpr_meta_append(&out, "}")) {
        free(out.data);
        cxpr_meta_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        return NULL;
    }
    return out.data;
}

int cxpr_meta_plugin_emit_manifest(
    const cxpr_plugin_model_event* event,
    const cxpr_meta_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    cxpr_plugin_artifact_event artifact = {
        "cxpr_meta_manifest",
        "cxpr.meta.manifest.v1",
        NULL
    };
    char* manifest;
    int ok;

    if (!event || !event->model || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        cxpr_meta_set_error(err, CXPR_ERR_SYNTAX, "cxpr meta plugin requires model and host callbacks");
        return 0;
    }
    manifest = cxpr_meta_plugin_manifest_from_model(event->model, options, err);
    if (!manifest) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, manifest, strlen(manifest), err) &&
         host->end_artifact(host->user, err);
    free(manifest);
    return ok ? 1 : 0;
}

void cxpr_meta_plugin_manifest_free(char* manifest) {
    free(manifest);
}
