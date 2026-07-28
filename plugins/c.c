#include <cxpr/plugins/c.h>
#include <cxpr/model/model.h>

#include <stdlib.h>
#include <string.h>

typedef struct cxpr_c_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_c_buf;

static int cxpr_c_append_bytes(cxpr_c_buf* b, const char* text, size_t n) {
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

static int cxpr_c_append(cxpr_c_buf* b, const char* text) {
    return cxpr_c_append_bytes(b, text, strlen(text));
}

char* cxpr_c_plugin_source_from_program(
    const cxpr_model_program* program,
    const cxpr_c_plugin_options* options,
    cxpr_error* err) {
    static const cxpr_c_plugin_options defaults = {
        "cxpr_model_tick",
        NULL,
        NULL,
        0u,
        NULL,
        0u,
        1
    };
    const cxpr_c_plugin_options* opts = options ? options : &defaults;
    const char* function_name = opts->function_name ? opts->function_name : defaults.function_name;
    char* body;
    cxpr_c_buf out = {0};

    if (opts->param_values && opts->param_count > 0u) {
        body = cxpr_model_program_to_c_tick_function_with_params_select_outputs(
            program,
            opts->qualifiers,
            function_name,
            opts->param_values,
            opts->param_count,
            opts->output_indices,
            opts->output_count,
            err);
    } else {
        body = cxpr_model_program_to_c_tick_function_select_outputs(
            program,
            opts->qualifiers,
            function_name,
            opts->output_indices,
            opts->output_count,
            err);
    }
    if (!body) return NULL;

    if (opts->include_headers) {
        if (!cxpr_c_append(&out,
                           "#include <math.h>\n"
                           "#include <stddef.h>\n"
                           "#include <stdbool.h>\n\n")) {
            free(body);
            free(out.data);
            return NULL;
        }
    }
    if (!cxpr_c_append(&out, body)) {
        free(body);
        free(out.data);
        return NULL;
    }
    free(body);
    return out.data;
}

int cxpr_c_plugin_emit_source(
    const cxpr_plugin_model_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    cxpr_plugin_artifact_event artifact = {
        "cxpr_c_source",
        "cxpr.c.source.v1",
        NULL
    };
    char* source;
    int ok;

    if (!event || !event->program || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "cxpr C plugin requires program and host callbacks";
        }
        return 0;
    }
    source = cxpr_c_plugin_source_from_program(event->program, options, err);
    if (!source) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, source, strlen(source), err) &&
         host->end_artifact(host->user, err);
    free(source);
    return ok ? 1 : 0;
}

void cxpr_c_plugin_source_free(char* source) {
    free(source);
}

static int cxpr_c_plugin_generate_bridge(
    const cxpr_plugin_model_event* event,
    const void* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    return cxpr_c_plugin_emit_source(
        event,
        (const cxpr_c_plugin_options*)options,
        host,
        err);
}

const cxpr_plugin_backend* cxpr_c_plugin_backend(void) {
    static const cxpr_plugin_backend backend = {
        "cxpr.c.source",
        cxpr_c_plugin_generate_bridge
    };
    return &backend;
}
