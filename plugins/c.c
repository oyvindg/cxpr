#include <cxpr/plugins/c.h>
#include <cxpr/context.h>
#include <cxpr/generated.h>
#include <cxpr/model/model.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cxpr_c_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_c_buf;

static int cxpr_c_reserve(cxpr_c_buf* b, size_t n) {
    if (!b) return 0;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 1024u;
        char* next;
        while (b->len + n + 1u > cap) cap *= 2u;
        next = (char*)realloc(b->data, cap);
        if (!next) return 0;
        b->data = next;
        b->cap = cap;
    }
    return 1;
}

static int cxpr_c_append_bytes(cxpr_c_buf* b, const char* text, size_t n) {
    if (!text || !cxpr_c_reserve(b, n)) return 0;
    memcpy(b->data + b->len, text, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int cxpr_c_append(cxpr_c_buf* b, const char* text) {
    return cxpr_c_append_bytes(b, text, strlen(text));
}

static int cxpr_c_appendf(cxpr_c_buf* b, const char* format, ...) {
    va_list args;
    va_list copy;
    int length;
    int written;

    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0u, format, copy);
    va_end(copy);
    if (length < 0) {
        va_end(args);
        return 0;
    }
    if (!cxpr_c_reserve(b, (size_t)length)) {
        va_end(args);
        return 0;
    }
    written = vsnprintf(b->data + b->len, (size_t)length + 1u, format, args);
    va_end(args);
    if (written != length) return 0;
    b->len += (size_t)length;
    return 1;
}

static void cxpr_c_set_error(cxpr_error* err,
                             cxpr_error_code code,
                             const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
}

static const char* cxpr_c_generated_type_name(cxpr_model_result_kind kind) {
    switch (kind) {
        case CXPR_MODEL_RESULT_NUMBER:
            return "CXPR_GENERATED_VALUE_NUMBER";
        case CXPR_MODEL_RESULT_BOOL:
            return "CXPR_GENERATED_VALUE_BOOL";
        default:
            /*
             * The generated tick ABI transports every scalar as double.
             * Older model graphs may not retain an inferred result kind for
             * aliases/outputs; those remain numeric unless proven boolean.
             */
            return "CXPR_GENERATED_VALUE_NUMBER";
    }
}

static cxpr_model_result_kind cxpr_c_call_param_result_kind(
    const cxpr_model_compiled* program,
    const char* name) {
    const size_t count = cxpr_model_compiled_param_count(program);
    size_t i;
    for (i = 0u; i < count; ++i) {
        const char* candidate = cxpr_model_compiled_param_name(program, i);
        if (candidate && name && strcmp(candidate, name) == 0) {
            return cxpr_model_compiled_param_result_kind(program, i);
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

char* cxpr_c_plugin_source_from_program(
    const cxpr_model_compiled* program,
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
        body = cxpr_model_compiled_generate_c_specialized(
            program,
            opts->qualifiers,
            function_name,
            opts->param_values,
            opts->param_count,
            opts->output_indices,
            opts->output_count,
            err);
    } else {
        body = cxpr_model_compiled_generate_c_outputs(
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

char* cxpr_c_plugin_artifact_from_program(
    const cxpr_model_compiled* program,
    const char* model_name,
    const cxpr_c_plugin_options* options,
    cxpr_error* err) {
    static const char* default_function_name = "cxpr_model_tick";
    const char* function_name =
        options && options->function_name ? options->function_name : default_function_name;
    const size_t input_count = cxpr_model_compiled_input_count(program);
    const size_t model_output_count = cxpr_model_compiled_output_count(program);
    const size_t output_count =
        options && options->output_count > 0u ? options->output_count : model_output_count;
    const size_t param_count = cxpr_model_compiled_call_param_count(program);
    const size_t* output_indices = options ? options->output_indices : NULL;
    cxpr_context* defaults_ctx = NULL;
    char* evaluator = NULL;
    cxpr_c_buf out = {0};
    size_t i;

    if (!program || !model_name || !model_name[0]) {
        cxpr_c_set_error(
            err, CXPR_ERR_SYNTAX, "cxpr C artifact requires program and model name");
        return NULL;
    }
    if (input_count > CXPR_GENERATED_MODEL_MAX_INPUTS ||
        output_count > CXPR_GENERATED_MODEL_MAX_OUTPUTS ||
        param_count > CXPR_GENERATED_MODEL_MAX_PARAMS) {
        cxpr_c_set_error(
            err, CXPR_ERR_SYNTAX, "generated descriptor exceeds ABI limits");
        return NULL;
    }
    if (options && options->output_count > 0u && !output_indices) {
        cxpr_c_set_error(
            err, CXPR_ERR_SYNTAX, "selected outputs require output indices");
        return NULL;
    }
    for (i = 0u; i < output_count && output_indices; ++i) {
        if (output_indices[i] >= model_output_count) {
            cxpr_c_set_error(
                err, CXPR_ERR_SYNTAX, "selected output index is out of range");
            return NULL;
        }
    }

    evaluator = cxpr_c_plugin_source_from_program(program, options, err);
    if (!evaluator) return NULL;
    defaults_ctx = cxpr_context_new();
    if (!defaults_ctx) {
        cxpr_c_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
        goto fail;
    }
    if (!cxpr_model_compiled_seed_defaults(program, defaults_ctx, NULL, err)) {
        goto fail;
    }
    if (!cxpr_c_append(&out, evaluator) ||
        !cxpr_c_appendf(
            &out,
            "\n#include <cxpr/generated.h>\n"
            "#include <string.h>\n"
            "static size_t %s_descriptor_state_size(void) {\n"
            "    return sizeof(%s_state);\n"
            "}\n"
            "static void %s_descriptor_reset(void* state) {\n"
            "    memset(state, 0, sizeof(%s_state));\n"
            "}\n"
            "static const cxpr_generated_model_descriptor %s_descriptor = {\n"
            "    .name = \"%s\",\n"
            "    .tick = (cxpr_generated_tick_fn)%s,\n"
            "    .state_size = %s_descriptor_state_size,\n"
            "    .reset = %s_descriptor_reset,\n"
            "    .param_count = %zu,\n",
            function_name, function_name,
            function_name, function_name,
            function_name, model_name,
            function_name, function_name, function_name, param_count)) {
        goto oom;
    }
    for (i = 0u; i < param_count; ++i) {
        const char* name = cxpr_model_compiled_call_param_name(program, i);
        const cxpr_model_result_kind kind =
            cxpr_c_call_param_result_kind(program, name);
        bool found = false;
        const double value = cxpr_context_get_param(defaults_ctx, name, &found);
        if (!cxpr_c_appendf(
                &out,
                "    .param_names[%zu] = \"%s\",\n"
                "    .param_types[%zu] = %s,\n"
                "    .param_defaults[%zu] = %.17g,\n"
                "    .param_has_default[%zu] = %uu,\n",
                i, name ? name : "",
                i, cxpr_c_generated_type_name(kind),
                i, value, i, found ? 1u : 0u)) {
            goto oom;
        }
    }
    if (!cxpr_c_appendf(&out, "    .input_count = %zu,\n", input_count)) {
        goto oom;
    }
    for (i = 0u; i < input_count; ++i) {
        if (!cxpr_c_appendf(
                &out,
                "    .input_names[%zu] = \"%s\",\n"
                "    .input_types[%zu] = %s,\n",
                i, cxpr_model_compiled_input_name(program, i),
                i, cxpr_c_generated_type_name(
                       cxpr_model_compiled_input_result_kind(program, i)))) {
            goto oom;
        }
    }
    if (!cxpr_c_appendf(&out, "    .output_count = %zu,\n", output_count)) {
        goto oom;
    }
    for (i = 0u; i < output_count; ++i) {
        const size_t model_index = output_indices ? output_indices[i] : i;
        if (!cxpr_c_appendf(
                &out,
                "    .output_names[%zu] = \"%s\",\n"
                "    .output_types[%zu] = %s,\n",
                i, cxpr_model_compiled_output_name(program, model_index),
                i, cxpr_c_generated_type_name(
                       cxpr_model_compiled_output_result_kind(
                           program, model_index)))) {
            goto oom;
        }
    }
    if (!cxpr_c_append(
            &out,
            "    .abi_version = CXPR_GENERATED_MODEL_ABI_VERSION,\n"
            "};\n")) {
        goto oom;
    }

    free(evaluator);
    cxpr_context_free(defaults_ctx);
    if (err) err->code = CXPR_OK;
    return out.data;

oom:
    cxpr_c_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
fail:
    free(evaluator);
    free(out.data);
    cxpr_context_free(defaults_ctx);
    return NULL;
}

int cxpr_c_plugin_emit_artifact(
    const cxpr_model_plugin_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    cxpr_model_plugin_artifact_event artifact = {
        "cxpr_c_artifact",
        "cxpr.c.source.v1",
        NULL
    };
    const char* model_name;
    char* source;
    int ok;

    if (!event || !event->compiled || !event->model || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        cxpr_c_set_error(
            err, CXPR_ERR_SYNTAX,
            "cxpr C artifact plugin requires model, program, and host callbacks");
        return 0;
    }
    model_name = cxpr_model_name(event->model);
    source = cxpr_c_plugin_artifact_from_program(
        event->compiled, model_name, options, err);
    if (!source) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, source, strlen(source), err) &&
         host->end_artifact(host->user, err);
    free(source);
    return ok ? 1 : 0;
}

int cxpr_c_plugin_emit_source(
    const cxpr_model_plugin_event* event,
    const cxpr_c_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    cxpr_model_plugin_artifact_event artifact = {
        "cxpr_c_source",
        "cxpr.c.source.v1",
        NULL
    };
    char* source;
    int ok;

    if (!event || !event->compiled || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        if (err) {
            err->code = CXPR_ERR_SYNTAX;
            err->message = "cxpr C plugin requires program and host callbacks";
        }
        return 0;
    }
    source = cxpr_c_plugin_source_from_program(event->compiled, options, err);
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
    const cxpr_model_plugin_event* event,
    const void* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    return cxpr_c_plugin_emit_source(
        event,
        (const cxpr_c_plugin_options*)options,
        host,
        err);
}

const cxpr_model_plugin_backend* cxpr_c_plugin_backend(void) {
    static const cxpr_model_plugin_backend backend = {
        "cxpr.c.source",
        cxpr_c_plugin_generate_bridge
    };
    return &backend;
}
