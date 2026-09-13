#include <cxpr/debug_map.h>
#include <cxpr/model/model.h>
#include <cxpr/plugins/debug_map.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct cxpr_debug_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_debug_buf;

typedef struct cxpr_debug_gen_node {
    cxpr_debug_node_id id;
    const char* name;
    cxpr_debug_node_kind kind;
    cxpr_debug_result_type result_type;
    char* canonical_source;
    cxpr_source_span span;
    int has_span;
    const cxpr_expr_ast* expr;
    cxpr_debug_node_id* dependencies;
    size_t dependency_count;
} cxpr_debug_gen_node;

static void cxpr_debug_set_error(
    cxpr_error* err,
    cxpr_error_code code,
    const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0u;
    err->line = 0u;
    err->column = 0u;
}

static int cxpr_debug_reserve(cxpr_debug_buf* out, size_t size) {
    size_t cap;
    char* grown;
    if (!out) return 0;
    if (out->len + size + 1u <= out->cap) return 1;
    cap = out->cap ? out->cap : 2048u;
    while (out->len + size + 1u > cap) cap *= 2u;
    grown = (char*)realloc(out->data, cap);
    if (!grown) return 0;
    out->data = grown;
    out->cap = cap;
    return 1;
}

static int cxpr_debug_append_bytes(
    cxpr_debug_buf* out,
    const char* text,
    size_t size) {
    if (!text || !cxpr_debug_reserve(out, size)) return 0;
    memcpy(out->data + out->len, text, size);
    out->len += size;
    out->data[out->len] = '\0';
    return 1;
}

static int cxpr_debug_append(cxpr_debug_buf* out, const char* text) {
    return cxpr_debug_append_bytes(out, text, strlen(text));
}

static char* cxpr_debug_strdup(const char* text) {
    const size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static int cxpr_debug_appendf(cxpr_debug_buf* out, const char* format, ...) {
    va_list args;
    va_list copy;
    int size;

    va_start(args, format);
    va_copy(copy, args);
    size = vsnprintf(NULL, 0u, format, copy);
    va_end(copy);
    if (size < 0 || !cxpr_debug_reserve(out, (size_t)size)) {
        va_end(args);
        return 0;
    }
    if (vsnprintf(out->data + out->len, (size_t)size + 1u, format, args) != size) {
        va_end(args);
        return 0;
    }
    va_end(args);
    out->len += (size_t)size;
    return 1;
}

static int cxpr_debug_append_c_string(cxpr_debug_buf* out, const char* text) {
    const unsigned char* cursor = (const unsigned char*)(text ? text : "");
    if (!cxpr_debug_append(out, "\"")) return 0;
    while (*cursor) {
        char escaped[5];
        switch (*cursor) {
            case '\"':
                if (!cxpr_debug_append(out, "\\\"")) return 0;
                break;
            case '\\':
                if (!cxpr_debug_append(out, "\\\\")) return 0;
                break;
            case '\n':
                if (!cxpr_debug_append(out, "\\n")) return 0;
                break;
            case '\r':
                if (!cxpr_debug_append(out, "\\r")) return 0;
                break;
            case '\t':
                if (!cxpr_debug_append(out, "\\t")) return 0;
                break;
            default:
                if (*cursor < 0x20u || *cursor >= 0x7fu) {
                    snprintf(escaped, sizeof(escaped), "\\%03o", *cursor);
                    if (!cxpr_debug_append(out, escaped)) return 0;
                } else if (!cxpr_debug_append_bytes(
                               out, (const char*)cursor, 1u)) {
                    return 0;
                }
                break;
        }
        ++cursor;
    }
    return cxpr_debug_append(out, "\"");
}

static cxpr_debug_node_id cxpr_debug_stable_id(
    const char* name_space,
    const char* name) {
    static const uint64_t offset = UINT64_C(14695981039346656037);
    static const uint64_t prime = UINT64_C(1099511628211);
    const unsigned char* cursor;
    uint64_t hash = offset;

    for (cursor = (const unsigned char*)name_space; *cursor; ++cursor) {
        hash = (hash ^ *cursor) * prime;
    }
    hash = (hash ^ (unsigned char)':') * prime;
    for (cursor = (const unsigned char*)name; *cursor; ++cursor) {
        hash = (hash ^ *cursor) * prime;
    }
    return hash ? hash : UINT64_C(1);
}

static const char* cxpr_debug_kind_key(cxpr_debug_node_kind kind) {
    switch (kind) {
        case CXPR_DEBUG_NODE_INPUT: return "input";
        case CXPR_DEBUG_NODE_PARAM: return "param";
        case CXPR_DEBUG_NODE_EXPRESSION: return "expression";
        case CXPR_DEBUG_NODE_STATE: return "state";
        case CXPR_DEBUG_NODE_STATE_UPDATE: return "state_update";
        default: return "unknown";
    }
}

static const char* cxpr_debug_kind_c_name(cxpr_debug_node_kind kind) {
    switch (kind) {
        case CXPR_DEBUG_NODE_INPUT: return "CXPR_DEBUG_NODE_INPUT";
        case CXPR_DEBUG_NODE_PARAM: return "CXPR_DEBUG_NODE_PARAM";
        case CXPR_DEBUG_NODE_EXPRESSION: return "CXPR_DEBUG_NODE_EXPRESSION";
        case CXPR_DEBUG_NODE_STATE: return "CXPR_DEBUG_NODE_STATE";
        case CXPR_DEBUG_NODE_STATE_UPDATE: return "CXPR_DEBUG_NODE_STATE_UPDATE";
        default: return "CXPR_DEBUG_NODE_EXPRESSION";
    }
}

static cxpr_debug_result_type cxpr_debug_result_type_from_model(
    cxpr_model_result_kind kind) {
    switch (kind) {
        case CXPR_MODEL_RESULT_NUMBER: return CXPR_DEBUG_RESULT_NUMBER;
        case CXPR_MODEL_RESULT_BOOL: return CXPR_DEBUG_RESULT_BOOL;
        default: return CXPR_DEBUG_RESULT_UNKNOWN;
    }
}

static const char* cxpr_debug_result_c_name(cxpr_debug_result_type type) {
    switch (type) {
        case CXPR_DEBUG_RESULT_NUMBER: return "CXPR_DEBUG_RESULT_NUMBER";
        case CXPR_DEBUG_RESULT_BOOL: return "CXPR_DEBUG_RESULT_BOOL";
        default: return "CXPR_DEBUG_RESULT_UNKNOWN";
    }
}

static cxpr_model_result_kind cxpr_debug_compiled_param_type(
    const cxpr_model_compiled* compiled,
    const char* name) {
    size_t i;
    for (i = 0u; i < cxpr_model_compiled_param_count(compiled); ++i) {
        if (strcmp(cxpr_model_compiled_param_name(compiled, i), name) == 0) {
            return cxpr_model_compiled_param_result_kind(compiled, i);
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static cxpr_model_result_kind cxpr_debug_compiled_binding_type(
    const cxpr_model_compiled* compiled,
    const char* name,
    cxpr_model_binding_kind kind) {
    size_t i;
    for (i = 0u; i < cxpr_model_compiled_binding_count(compiled); ++i) {
        if (cxpr_model_compiled_binding_kind(compiled, i) == kind &&
            strcmp(cxpr_model_compiled_binding_name(compiled, i), name) == 0) {
            return cxpr_model_compiled_binding_result_kind(compiled, i);
        }
    }
    return CXPR_MODEL_RESULT_UNKNOWN;
}

static cxpr_debug_node_kind cxpr_debug_binding_kind(
    cxpr_model_binding_kind kind) {
    switch (kind) {
        case CXPR_MODEL_BINDING_STATE: return CXPR_DEBUG_NODE_STATE;
        case CXPR_MODEL_BINDING_STATE_UPDATE: return CXPR_DEBUG_NODE_STATE_UPDATE;
        default: return CXPR_DEBUG_NODE_EXPRESSION;
    }
}

static cxpr_debug_gen_node* cxpr_debug_find_ref_node(
    cxpr_debug_gen_node* nodes,
    size_t count,
    const char* name) {
    static const cxpr_debug_node_kind precedence[] = {
        CXPR_DEBUG_NODE_INPUT,
        CXPR_DEBUG_NODE_PARAM,
        CXPR_DEBUG_NODE_EXPRESSION,
        CXPR_DEBUG_NODE_STATE
    };
    size_t p;
    size_t i;
    for (p = 0u; p < sizeof(precedence) / sizeof(precedence[0]); ++p) {
        for (i = 0u; i < count; ++i) {
            if (nodes[i].kind == precedence[p] &&
                strcmp(nodes[i].name, name) == 0) {
                return &nodes[i];
            }
        }
    }
    return NULL;
}

static int cxpr_debug_identifier_valid(const char* name) {
    const unsigned char* cursor = (const unsigned char*)name;
    if (!cursor || !(cursor[0] == '_' ||
                     (cursor[0] >= 'A' && cursor[0] <= 'Z') ||
                     (cursor[0] >= 'a' && cursor[0] <= 'z'))) {
        return 0;
    }
    for (++cursor; *cursor; ++cursor) {
        if (!(*cursor == '_' ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9'))) {
            return 0;
        }
    }
    return 1;
}

static void cxpr_debug_nodes_free(cxpr_debug_gen_node* nodes, size_t count) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        free(nodes[i].canonical_source);
        free(nodes[i].dependencies);
    }
    free(nodes);
}

static int cxpr_debug_build_dependencies(
    cxpr_debug_gen_node* nodes,
    size_t count) {
    size_t i;
    for (i = 0u; i < count; ++i) {
        const char** refs;
        size_t ref_count;
        size_t j;
        if (!nodes[i].expr) continue;
        ref_count = cxpr_expr_ast_references(nodes[i].expr, NULL, 0u);
        if (ref_count == 0u) continue;
        refs = (const char**)calloc(ref_count, sizeof(*refs));
        if (!refs) return 0;
        ref_count = cxpr_expr_ast_references(nodes[i].expr, refs, ref_count);
        nodes[i].dependencies =
            (cxpr_debug_node_id*)calloc(ref_count, sizeof(*nodes[i].dependencies));
        if (!nodes[i].dependencies) {
            free(refs);
            return 0;
        }
        for (j = 0u; j < ref_count; ++j) {
            cxpr_debug_gen_node* dependency =
                cxpr_debug_find_ref_node(nodes, count, refs[j]);
            size_t k;
            int duplicate = 0;
            if (!dependency) continue;
            for (k = 0u; k < nodes[i].dependency_count; ++k) {
                if (nodes[i].dependencies[k] == dependency->id) {
                    duplicate = 1;
                    break;
                }
            }
            if (!duplicate) {
                nodes[i].dependencies[nodes[i].dependency_count++] =
                    dependency->id;
            }
        }
        free(refs);
        if (nodes[i].dependency_count == 0u) {
            free(nodes[i].dependencies);
            nodes[i].dependencies = NULL;
        }
    }
    return 1;
}

char* cxpr_debug_map_plugin_source_from_model(
    const cxpr_model* model,
    const cxpr_model_compiled* compiled,
    const char* source_path,
    const cxpr_debug_map_plugin_options* options,
    cxpr_error* err) {
    static const char* default_symbol = "cxpr_model_debug_map";
    static const char* default_qualifiers = "static const";
    const char* symbol =
        options && options->symbol_name ? options->symbol_name : default_symbol;
    const char* qualifiers =
        options && options->qualifiers ? options->qualifiers : default_qualifiers;
    const size_t input_count = cxpr_model_compiled_input_count(compiled);
    const size_t param_count = cxpr_model_constant_count(model);
    const size_t binding_count = cxpr_model_binding_count(model);
    const size_t node_count = input_count + param_count + binding_count;
    const size_t model_output_count = cxpr_model_compiled_output_count(compiled);
    const size_t output_count =
        options && options->output_count > 0u
            ? options->output_count
            : model_output_count;
    const size_t* output_indices = options ? options->output_indices : NULL;
    cxpr_debug_gen_node* nodes = NULL;
    cxpr_debug_buf out = {0};
    size_t cursor = 0u;
    size_t i;

    if (!model || !compiled || !cxpr_debug_identifier_valid(symbol)) {
        cxpr_debug_set_error(
            err, CXPR_ERR_SYNTAX,
            "cxpr debug map requires model, compiled program, and valid symbol");
        return NULL;
    }
    if (options && options->output_count > 0u && !output_indices) {
        cxpr_debug_set_error(
            err, CXPR_ERR_SYNTAX, "selected debug outputs require indices");
        return NULL;
    }
    nodes = (cxpr_debug_gen_node*)calloc(node_count, sizeof(*nodes));
    if (node_count > 0u && !nodes) goto oom;

    for (i = 0u; i < input_count; ++i, ++cursor) {
        const char* name = cxpr_model_compiled_input_name(compiled, i);
        size_t parsed_i;
        nodes[cursor].name = name;
        nodes[cursor].kind = CXPR_DEBUG_NODE_INPUT;
        nodes[cursor].id = cxpr_debug_stable_id("input", name);
        nodes[cursor].result_type = cxpr_debug_result_type_from_model(
            cxpr_model_compiled_input_result_kind(compiled, i));
        nodes[cursor].canonical_source = cxpr_debug_strdup(name);
        for (parsed_i = 0u; parsed_i < cxpr_model_input_count(model); ++parsed_i) {
            if (strcmp(cxpr_model_input(model, parsed_i), name) == 0) {
                nodes[cursor].has_span = cxpr_model_input_source_span(
                    model, parsed_i, &nodes[cursor].span);
                break;
            }
        }
        if (!nodes[cursor].canonical_source) goto oom;
    }
    for (i = 0u; i < param_count; ++i, ++cursor) {
        const char* name = cxpr_model_constant_name(model, i);
        nodes[cursor].name = name;
        nodes[cursor].kind = CXPR_DEBUG_NODE_PARAM;
        nodes[cursor].id = cxpr_debug_stable_id("param", name);
        nodes[cursor].result_type = cxpr_debug_result_type_from_model(
            cxpr_debug_compiled_param_type(compiled, name));
        nodes[cursor].expr = cxpr_model_constant_expr(model, i);
        nodes[cursor].canonical_source =
            cxpr_expr_ast_to_string(nodes[cursor].expr);
        nodes[cursor].has_span = cxpr_model_constant_source_span(
            model, i, &nodes[cursor].span);
        if (!nodes[cursor].canonical_source) goto oom;
    }
    for (i = 0u; i < binding_count; ++i, ++cursor) {
        const char* name = cxpr_model_binding_name(model, i);
        const cxpr_model_binding_kind kind =
            cxpr_model_binding_kind_at(model, i);
        nodes[cursor].name = name;
        nodes[cursor].kind = cxpr_debug_binding_kind(kind);
        nodes[cursor].id =
            cxpr_debug_stable_id(cxpr_debug_kind_key(nodes[cursor].kind), name);
        nodes[cursor].result_type = cxpr_debug_result_type_from_model(
            cxpr_debug_compiled_binding_type(compiled, name, kind));
        nodes[cursor].expr = cxpr_model_binding_expr(model, i);
        nodes[cursor].canonical_source =
            cxpr_expr_ast_to_string(nodes[cursor].expr);
        nodes[cursor].has_span = cxpr_model_binding_source_span(
            model, i, &nodes[cursor].span);
        if (!nodes[cursor].canonical_source) goto oom;
    }
    if (!cxpr_debug_build_dependencies(nodes, node_count)) goto oom;

    for (i = 0u; i < node_count; ++i) {
        size_t j;
        for (j = i + 1u; j < node_count; ++j) {
            if (nodes[i].id == nodes[j].id) {
                cxpr_debug_set_error(
                    err, CXPR_ERR_SYNTAX, "cxpr debug map node ID collision");
                goto fail;
            }
        }
    }
    for (i = 0u; i < output_count; ++i) {
        const size_t output_index = output_indices ? output_indices[i] : i;
        const char* name;
        if (output_index >= model_output_count) {
            cxpr_debug_set_error(
                err, CXPR_ERR_SYNTAX, "debug output index is out of range");
            goto fail;
        }
        name = cxpr_model_compiled_output_name(compiled, output_index);
        if (!cxpr_debug_find_ref_node(nodes, node_count, name)) {
            cxpr_debug_set_error(
                err, CXPR_ERR_SYNTAX, "debug output has no source node");
            goto fail;
        }
    }

    if (!cxpr_debug_append(
            &out,
            "#include <cxpr/debug_map.h>\n"
            "#include <stdint.h>\n\n")) {
        goto oom;
    }
    for (i = 0u; i < node_count; ++i) {
        size_t j;
        if (nodes[i].dependency_count == 0u) continue;
        if (!cxpr_debug_appendf(
                &out,
                "static const cxpr_debug_node_id %s_node_%zu_dependencies[] = {",
                symbol, i)) {
            goto oom;
        }
        for (j = 0u; j < nodes[i].dependency_count; ++j) {
            if ((j > 0u && !cxpr_debug_append(&out, ", ")) ||
                !cxpr_debug_appendf(
                    &out, "UINT64_C(%llu)",
                    (unsigned long long)nodes[i].dependencies[j])) {
                goto oom;
            }
        }
        if (!cxpr_debug_append(&out, "};\n")) goto oom;
    }
    if (!cxpr_debug_appendf(
            &out,
            "\nstatic const cxpr_debug_node %s_nodes[] = {\n",
            symbol)) {
        goto oom;
    }
    for (i = 0u; i < node_count; ++i) {
        const cxpr_source_span zero = {0};
        const cxpr_source_span* span =
            nodes[i].has_span ? &nodes[i].span : &zero;
        if (i > 0u && !cxpr_debug_append(&out, ",\n")) goto oom;
        if (!cxpr_debug_appendf(
                &out,
                "    {\n"
                "        .id = UINT64_C(%llu),\n"
                "        .name = ",
                (unsigned long long)nodes[i].id) ||
            !cxpr_debug_append_c_string(&out, nodes[i].name) ||
            !cxpr_debug_appendf(
                &out,
                ",\n"
                "        .kind = %s,\n"
                "        .result_type = %s,\n"
                "        .source_path = ",
                cxpr_debug_kind_c_name(nodes[i].kind),
                cxpr_debug_result_c_name(nodes[i].result_type))) {
            goto oom;
        }
        if (nodes[i].has_span && source_path && source_path[0]) {
            if (!cxpr_debug_append_c_string(&out, source_path)) goto oom;
        } else if (!cxpr_debug_append(&out, "NULL")) {
            goto oom;
        }
        if (!cxpr_debug_appendf(
                &out,
                ",\n"
                "        .source_span = {%zuu, %zuu, %zuu, %zuu, %zuu, %zuu},\n"
                "        .has_source_span = %d,\n"
                "        .canonical_source = ",
                span->start.offset, span->start.line, span->start.column,
                span->end.offset, span->end.line, span->end.column,
                nodes[i].has_span ? 1 : 0) ||
            !cxpr_debug_append_c_string(&out, nodes[i].canonical_source)) {
            goto oom;
        }
        if (nodes[i].dependency_count > 0u) {
            if (!cxpr_debug_appendf(
                    &out,
                    ",\n"
                    "        .dependencies = %s_node_%zu_dependencies,\n",
                    symbol, i)) {
                goto oom;
            }
        } else if (!cxpr_debug_append(
                       &out, ",\n        .dependencies = NULL,\n")) {
            goto oom;
        }
        if (!cxpr_debug_appendf(
                &out,
                "        .dependency_count = %zuu,\n"
                "        .trace_slot = CXPR_DEBUG_TRACE_SLOT_NONE,\n"
                "    }",
                nodes[i].dependency_count)) {
            goto oom;
        }
    }
    if (!cxpr_debug_appendf(
            &out,
            "\n};\n\n"
            "static const cxpr_debug_output %s_outputs[] = {\n",
            symbol)) {
        goto oom;
    }
    for (i = 0u; i < output_count; ++i) {
        const size_t output_index = output_indices ? output_indices[i] : i;
        const char* name =
            cxpr_model_compiled_output_name(compiled, output_index);
        const cxpr_debug_gen_node* node =
            cxpr_debug_find_ref_node(nodes, node_count, name);
        if (i > 0u && !cxpr_debug_append(&out, ",\n")) goto oom;
        if (!cxpr_debug_appendf(
                &out,
                "    {\n"
                "        .id = UINT64_C(%llu),\n"
                "        .name = ",
                (unsigned long long)cxpr_debug_stable_id("output", name)) ||
            !cxpr_debug_append_c_string(&out, name) ||
            !cxpr_debug_appendf(
                &out,
                ",\n"
                "        .node_id = UINT64_C(%llu),\n"
                "        .result_type = %s,\n"
                "    }",
                (unsigned long long)node->id,
                cxpr_debug_result_c_name(cxpr_debug_result_type_from_model(
                    cxpr_model_compiled_output_result_kind(
                        compiled, output_index))))) {
            goto oom;
        }
    }
    if (!cxpr_debug_appendf(&out,
                            "\n};\n\n%s cxpr_debug_map %s = {\n"
                            "    .abi_version = CXPR_DEBUG_MAP_ABI_VERSION,\n"
                            "    .model_name = ",
                            qualifiers, symbol) ||
        !cxpr_debug_append_c_string(&out, cxpr_model_name(model)) ||
        !cxpr_debug_appendf(
            &out,
            ",\n"
            "    .nodes = %s_nodes,\n"
            "    .node_count = %zuu,\n"
            "    .outputs = %s_outputs,\n"
            "    .output_count = %zuu,\n"
            "};\n",
            symbol, node_count, symbol, output_count)) {
        goto oom;
    }

    cxpr_debug_nodes_free(nodes, node_count);
    if (err) err->code = CXPR_OK;
    return out.data;

oom:
    cxpr_debug_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "out of memory");
fail:
    free(out.data);
    cxpr_debug_nodes_free(nodes, node_count);
    return NULL;
}

void cxpr_debug_map_plugin_source_free(char* source) {
    free(source);
}

int cxpr_debug_map_plugin_emit(
    const cxpr_model_plugin_event* event,
    const cxpr_debug_map_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    cxpr_model_plugin_artifact_event artifact = {
        "cxpr_debug_map",
        "cxpr.debug-map.c.v1",
        NULL
    };
    char* source;
    int ok;
    if (!event || !event->model || !event->compiled || !host ||
        !host->begin_artifact || !host->write_artifact ||
        !host->end_artifact) {
        cxpr_debug_set_error(
            err, CXPR_ERR_SYNTAX,
            "cxpr debug map plugin requires model, program, and host callbacks");
        return 0;
    }
    source = cxpr_debug_map_plugin_source_from_model(
        event->model, event->compiled, event->model_path, options, err);
    if (!source) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    artifact.path_hint = event->model_path;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, source, strlen(source), err) &&
         host->end_artifact(host->user, err);
    free(source);
    return ok ? 1 : 0;
}

static int cxpr_debug_map_backend_generate(
    const cxpr_model_plugin_event* event,
    const void* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    return cxpr_debug_map_plugin_emit(
        event, (const cxpr_debug_map_plugin_options*)options, host, err);
}

const cxpr_model_plugin_backend* cxpr_debug_map_plugin_backend(void) {
    static const cxpr_model_plugin_backend backend = {
        "cxpr.debug-map.c",
        cxpr_debug_map_backend_generate
    };
    return &backend;
}
