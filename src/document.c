#include <cxpr/document/document.h>
#include <cxpr/ast/document.h>
#include <cxpr/alias.h>
#include <cxpr/parser.h>

#include "model/internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cxpr_document {
    cxpr_model* model;
    cxpr_document_ast* syntax;
    unsigned extensions;
};

static void cxpr_document_set_error(cxpr_error* err,
                                    cxpr_error_code code,
                                    const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->line = 0u;
    err->column = 0u;
}

static bool cxpr_document_model_constructs_empty(const cxpr_model* model,
                                                 cxpr_error* err) {
    if (!model) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document model");
        return false;
    }
    if ((cxpr_model_name(model) && cxpr_model_name(model)[0] != '\0') ||
        cxpr_model_use_count(model) != 0u ||
        cxpr_model_input_count(model) != 0u ||
        cxpr_model_constant_count(model) != 0u ||
        cxpr_model_binding_count(model) != 0u ||
        cxpr_model_output_count(model) != 0u ||
        cxpr_model_metadata_count(model) != 0u) {
        cxpr_document_set_error(
            err,
            CXPR_ERR_SYNTAX,
            "Model syntax requires CXPR_DOCUMENT_EXTENSION_MODEL");
        return false;
    }
    return true;
}

typedef struct {
    cxpr_model* model;
    size_t next_use;
    size_t next_input;
    size_t next_constant;
    size_t next_binding;
    size_t next_output;
    size_t next_host_block;
} cxpr_document_span_mapper;

static bool cxpr_document_names_match(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

static void cxpr_document_model_prepare_span_storage(cxpr_model* model) {
    if (!model) return;
    if (model->use_count > 0u && !model->use_spans) {
        model->use_spans = (cxpr_source_span*)calloc(model->use_count,
                                                     sizeof(*model->use_spans));
        model->use_has_spans = (bool*)calloc(model->use_count,
                                             sizeof(*model->use_has_spans));
    }
    if (model->input_count > 0u && !model->input_spans) {
        model->input_spans = (cxpr_source_span*)calloc(model->input_count,
                                                       sizeof(*model->input_spans));
        model->input_has_spans = (bool*)calloc(model->input_count,
                                               sizeof(*model->input_has_spans));
    }
    if (model->output_count > 0u && !model->output_spans) {
        model->output_spans = (cxpr_source_span*)calloc(model->output_count,
                                                        sizeof(*model->output_spans));
        model->output_has_spans = (bool*)calloc(model->output_count,
                                                sizeof(*model->output_has_spans));
    }
}

static void cxpr_document_map_use_span(cxpr_document_span_mapper* mapper,
                                       const char* text,
                                       cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->use_spans || !model->use_has_spans) return;
    (void)text;
    if (mapper->next_use < model->use_count) {
        model->use_spans[mapper->next_use] = span;
        model->use_has_spans[mapper->next_use] = true;
        mapper->next_use++;
    }
}

static void cxpr_document_map_input_span(cxpr_document_span_mapper* mapper,
                                         const char* name,
                                         cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->input_spans || !model->input_has_spans) return;
    for (size_t i = mapper->next_input; i < model->input_count; ++i) {
        if (!cxpr_document_names_match(model->inputs[i], name)) continue;
        model->input_spans[i] = span;
        model->input_has_spans[i] = true;
        mapper->next_input = i + 1u;
        return;
    }
}

static void cxpr_document_map_constant_span(cxpr_document_span_mapper* mapper,
                                            const char* name,
                                            cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_constant; i < model->constant_count; ++i) {
        if (!cxpr_document_names_match(model->constants[i].name, name)) continue;
        model->constants[i].span = span;
        model->constants[i].has_span = true;
        mapper->next_constant = i + 1u;
        return;
    }
}

static void cxpr_document_map_binding_span(cxpr_document_span_mapper* mapper,
                                           const char* name,
                                           cxpr_model_binding_kind kind,
                                           cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_binding; i < model->binding_count; ++i) {
        if (model->bindings[i].kind != kind ||
            !cxpr_document_names_match(model->bindings[i].name, name)) {
            continue;
        }
        model->bindings[i].span = span;
        model->bindings[i].has_span = true;
        mapper->next_binding = i + 1u;
        return;
    }
}

static void cxpr_document_map_output_span(cxpr_document_span_mapper* mapper,
                                          const char* name,
                                          cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->output_spans || !model->output_has_spans) return;
    for (size_t i = mapper->next_output; i < model->output_count; ++i) {
        if (!cxpr_document_names_match(model->outputs[i], name)) continue;
        model->output_spans[i] = span;
        model->output_has_spans[i] = true;
        mapper->next_output = i + 1u;
        return;
    }
}

static void cxpr_document_map_host_block_span(cxpr_document_span_mapper* mapper,
                                              const char* kind,
                                              cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_host_block; i < model->host_block_count; ++i) {
        if (!cxpr_document_names_match(model->host_blocks[i].kind, kind)) continue;
        model->host_blocks[i].span = span;
        model->host_blocks[i].has_span = true;
        mapper->next_host_block = i + 1u;
        return;
    }
}

static cxpr_visit_control cxpr_document_map_source_span_node(
    const cxpr_document_ast_node* node,
    void* userdata) {
    cxpr_document_span_mapper* mapper = (cxpr_document_span_mapper*)userdata;
    const char* name = cxpr_document_ast_node_name(node);
    cxpr_source_span span = cxpr_document_ast_node_span(node);

    switch (cxpr_document_ast_node_kind(node)) {
        case CXPR_DOCUMENT_AST_HOST_BLOCK:
            cxpr_document_map_host_block_span(mapper, name, span);
            break;
        case CXPR_DOCUMENT_AST_MODEL_DECL:
            if (mapper->model && cxpr_document_names_match(mapper->model->name, name)) {
                mapper->model->name_span = span;
                mapper->model->has_name_span = true;
            }
            break;
        case CXPR_MODEL_AST_USE:
            cxpr_document_map_use_span(mapper, cxpr_document_ast_node_text(node), span);
            break;
        case CXPR_MODEL_AST_INPUT_DECL:
            cxpr_document_map_input_span(mapper, name, span);
            break;
        case CXPR_MODEL_AST_PARAM_DECL:
            cxpr_document_map_constant_span(mapper, name, span);
            break;
        case CXPR_MODEL_AST_STATE_DECL:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE, span);
            break;
        case CXPR_MODEL_AST_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            break;
        case CXPR_MODEL_AST_INITIAL_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            break;
        case CXPR_MODEL_AST_BINDING:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_EXPR, span);
            break;
        case CXPR_MODEL_AST_OUTPUT_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            cxpr_document_map_output_span(mapper, name, span);
            break;
        case CXPR_MODEL_AST_OUTPUT_DECL:
            if (cxpr_document_ast_node_expression(node)) {
                cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_EXPR, span);
            }
            cxpr_document_map_output_span(mapper, name, span);
            break;
        default:
            break;
    }
    return CXPR_VISIT_CONTINUE;
}

static bool cxpr_document_span_for_metadata_target(const cxpr_model* model,
                                                   const cxpr_model_metadata* metadata,
                                                   cxpr_source_span* out_span) {
    if (!model || !metadata || !out_span) return false;
    switch (metadata->target_kind) {
        case CXPR_MODEL_METADATA_TARGET_MODEL:
            if (model->has_name_span) {
                *out_span = model->name_span;
                return true;
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_USE:
            for (size_t i = 0u; i < model->use_count; ++i) {
                if (model->use_has_spans && model->use_has_spans[i]) {
                    *out_span = model->use_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_INPUT:
            for (size_t i = 0u; i < model->input_count; ++i) {
                if (cxpr_document_names_match(model->inputs[i], metadata->target_name) &&
                    model->input_has_spans && model->input_has_spans[i]) {
                    *out_span = model->input_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_PARAM:
            for (size_t i = 0u; i < model->constant_count; ++i) {
                if (cxpr_document_names_match(model->constants[i].name, metadata->target_name) &&
                    model->constants[i].has_span) {
                    *out_span = model->constants[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_STATE:
            for (size_t i = 0u; i < model->binding_count; ++i) {
                if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
                    cxpr_document_names_match(model->bindings[i].name, metadata->target_name) &&
                    model->bindings[i].has_span) {
                    *out_span = model->bindings[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_BINDING:
            for (size_t i = 0u; i < model->binding_count; ++i) {
                if (cxpr_document_names_match(model->bindings[i].name, metadata->target_name) &&
                    model->bindings[i].has_span) {
                    *out_span = model->bindings[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_OUTPUT:
            for (size_t i = 0u; i < model->output_count; ++i) {
                if (cxpr_document_names_match(model->outputs[i], metadata->target_name) &&
                    model->output_has_spans && model->output_has_spans[i]) {
                    *out_span = model->output_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_FUNCTION:
        default:
            return false;
    }
}

static void cxpr_document_map_metadata_spans(cxpr_model* model) {
    if (!model) return;
    for (size_t i = 0u; i < model->metadata_count; ++i) {
        cxpr_source_span span;
        if (cxpr_document_span_for_metadata_target(model, &model->metadatas[i], &span)) {
            model->metadatas[i].span = span;
            model->metadatas[i].has_span = true;
        }
    }
}

static void cxpr_document_map_source_spans(cxpr_model* model,
                                           const cxpr_document_ast* syntax) {
    cxpr_document_span_mapper mapper;
    if (!model || !syntax) return;
    cxpr_document_model_prepare_span_storage(model);
    mapper = (cxpr_document_span_mapper){0};
    mapper.model = model;
    (void)cxpr_document_ast_visit(syntax, cxpr_document_map_source_span_node, &mapper);
    cxpr_document_map_metadata_spans(model);
}

static bool cxpr_document_lower_is_ident(const char* s) {
    if (!s || !(isalpha((unsigned char)*s) || *s == '_')) return false;
    for (s++; *s; ++s) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) return false;
    }
    return true;
}

static bool cxpr_document_lower_is_dotted_ident(const char* s) {
    bool need_ident_start = true;
    if (!s || *s == '\0') return false;
    while (*s) {
        if (need_ident_start) {
            if (!(isalpha((unsigned char)*s) || *s == '_')) return false;
            need_ident_start = false;
        } else if (*s == '.') {
            need_ident_start = true;
        } else if (!(isalnum((unsigned char)*s) || *s == '_')) {
            return false;
        }
        s++;
    }
    return !need_ident_start;
}

static bool cxpr_document_lower_is_use_path(const char* s) {
    const char* segment = s;
    if (!s || *s == '\0' || *s == '/') return false;
    while (*segment) {
        const char* slash = strchr(segment, '/');
        size_t len = slash ? (size_t)(slash - segment) : strlen(segment);
        bool last = slash == NULL;
        if (len == 0u) return false;
        if (last && len > 5u && strcmp(segment + len - 5u, ".cxpr") == 0) len -= 5u;
        if (len == 0u) return false;
        for (size_t i = 0u; i < len; ++i) {
            unsigned char ch = (unsigned char)segment[i];
            if (i == 0u) {
                if (!(isalpha(ch) || ch == '_')) return false;
            } else if (!(isalnum(ch) || ch == '_')) {
                return false;
            }
        }
        if (!slash) break;
        segment = slash + 1;
    }
    return true;
}

static bool cxpr_document_lower_has_top_level_comma(const char* text) {
    int paren = 0;
    int brace = 0;
    int bracket = 0;
    char quote = '\0';
    while (text && *text) {
        char ch = *text;
        if (quote) {
            if (ch == '\\' && text[1]) {
                text += 2;
                continue;
            }
            if (ch == quote) quote = '\0';
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') paren++;
        else if (ch == ')' && paren > 0) paren--;
        else if (ch == '{') brace++;
        else if (ch == '}' && brace > 0) brace--;
        else if (ch == '[') bracket++;
        else if (ch == ']' && bracket > 0) bracket--;
        else if (ch == ',' && paren == 0 && brace == 0 && bracket == 0) return true;
        text++;
    }
    return false;
}

static bool cxpr_document_function_block_is_lowerable(
    const cxpr_document_ast_node* node) {
    const cxpr_document_ast_node* body;
    bool saw_return = false;
    if (!node || cxpr_document_ast_child_count(node) != 1u) return false;
    body = cxpr_document_ast_child(node, 0u);
    if (!body || cxpr_document_ast_node_kind(body) != CXPR_MODEL_AST_FUNCTION_BODY) return false;
    for (size_t i = 0u; i < cxpr_document_ast_child_count(body); ++i) {
        const cxpr_document_ast_node* child = cxpr_document_ast_child(body, i);
        switch (cxpr_document_ast_node_kind(child)) {
            case CXPR_MODEL_AST_LOCAL_BINDING:
                if (saw_return || !cxpr_document_ast_node_expression(child)) return false;
                break;
            case CXPR_MODEL_AST_RETURN:
                if (saw_return || !cxpr_document_ast_node_text(child)) return false;
                saw_return = true;
                break;
            default:
                return false;
        }
    }
    return saw_return;
}

static bool cxpr_document_model_append_string(char*** values,
                                              size_t* count,
                                              const char* value) {
    char** grown;
    if (!values || !count || !value) return false;
    grown = (char**)realloc(*values, (*count + 1u) * sizeof(**values));
    if (!grown) return false;
    *values = grown;
    (*values)[*count] = cxpr_strdup(value);
    if (!(*values)[*count]) return false;
    (*count)++;
    return true;
}

static bool cxpr_document_model_append_use(cxpr_model* model,
                                           const char* path,
                                           const char* alias) {
    char** next_uses;
    char** next_aliases;
    size_t next_count;
    if (!model || !path) return false;
    next_count = model->use_count + 1u;
    next_uses = (char**)realloc(model->uses, next_count * sizeof(*model->uses));
    if (!next_uses) return false;
    model->uses = next_uses;
    next_aliases = (char**)realloc(model->use_aliases,
                                   next_count * sizeof(*model->use_aliases));
    if (!next_aliases) return false;
    model->use_aliases = next_aliases;
    model->uses[model->use_count] = cxpr_strdup(path);
    model->use_aliases[model->use_count] = alias ? cxpr_strdup(alias) : NULL;
    if (!model->uses[model->use_count] ||
        (alias && !model->use_aliases[model->use_count])) {
        return false;
    }
    model->use_count = next_count;
    return true;
}

static char* cxpr_document_lower_trim_in_place(char* s) {
    char* end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static bool cxpr_document_lower_use_clause(cxpr_model* model,
                                           char* text,
                                           cxpr_error* err) {
    char* cursor;
    char* as_kw;
    const char* alias = NULL;
    if (!model || !text) return false;
    cursor = cxpr_document_lower_trim_in_place(text);
    as_kw = strstr(cursor, " as ");
    if (as_kw) {
        char* alias_text;
        *as_kw = '\0';
        alias_text = cxpr_document_lower_trim_in_place(as_kw + 4);
        cursor = cxpr_document_lower_trim_in_place(cursor);
        if (!cxpr_document_lower_is_ident(alias_text)) {
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use name");
            return false;
        }
        alias = alias_text;
    }
    if (!cxpr_document_lower_is_use_path(cursor)) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use name");
        return false;
    }
    if (!cxpr_document_model_append_use(model, cursor, alias)) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    return true;
}

static bool cxpr_document_lower_use_group(cxpr_model* model,
                                          char* text,
                                          cxpr_error* err) {
    char* cursor;
    char* close;
    char* after;
    char* base;
    char* item;
    bool saw_item = false;
    if (!model || !text) return false;
    cursor = cxpr_document_lower_trim_in_place(text);
    if (*cursor != '{') return false;
    close = strchr(cursor, '}');
    if (!close) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    *close = '\0';
    after = cxpr_document_lower_trim_in_place(close + 1);
    if (strncmp(after, "from", 4u) != 0 ||
        (after[4] != '\0' && !isspace((unsigned char)after[4]))) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    base = cxpr_document_lower_trim_in_place(after + 4);
    if (!cxpr_document_lower_is_use_path(base)) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    item = cursor + 1;
    while (item) {
        char* next = strchr(item, ',');
        char* path;
        size_t path_len;
        if (next) *next = '\0';
        item = cxpr_document_lower_trim_in_place(item);
        if (!cxpr_document_lower_is_ident(item)) {
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
            return false;
        }
        path_len = strlen(base) + 1u + strlen(item);
        path = (char*)malloc(path_len + 1u);
        if (!path) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
        snprintf(path, path_len + 1u, "%s/%s", base, item);
        if (!cxpr_document_model_append_use(model, path, NULL)) {
            free(path);
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
        free(path);
        saw_item = true;
        item = next ? next + 1 : NULL;
    }
    if (!saw_item) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    return true;
}

static char* cxpr_document_lower_find_keyword(char* text, const char* keyword) {
    size_t len;
    size_t text_len;
    if (!text || !keyword) return NULL;
    len = strlen(keyword);
    if (len == 0u) return NULL;
    text_len = strlen(text);
    for (char* cursor = text; (size_t)(cursor - text) + len <= text_len; ++cursor) {
        bool before = cursor == text || isspace((unsigned char)cursor[-1]);
        bool after = cursor[len] == '\0' || isspace((unsigned char)cursor[len]);
        if (before && after && strncmp(cursor, keyword, len) == 0) return cursor;
    }
    return NULL;
}

static bool cxpr_document_lower_use_from_list(cxpr_model* model,
                                              char* text,
                                              cxpr_error* err) {
    char* cursor;
    char* from_kw;
    char* base;
    char* item;
    bool saw_item = false;
    if (!model || !text) return false;
    cursor = cxpr_document_lower_trim_in_place(text);
    from_kw = cxpr_document_lower_find_keyword(cursor, "from");
    if (!from_kw) return false;
    *from_kw = '\0';
    base = cxpr_document_lower_trim_in_place(from_kw + 4);
    if (!cxpr_document_lower_is_use_path(base)) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    item = cursor;
    while (item) {
        char* next = strchr(item, ',');
        char* path;
        size_t path_len;
        if (next) *next = '\0';
        item = cxpr_document_lower_trim_in_place(item);
        if (!cxpr_document_lower_is_ident(item)) {
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
            return false;
        }
        path_len = strlen(base) + 1u + strlen(item);
        path = (char*)malloc(path_len + 1u);
        if (!path) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
        snprintf(path, path_len + 1u, "%s/%s", base, item);
        if (!cxpr_document_model_append_use(model, path, NULL)) {
            free(path);
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
        free(path);
        saw_item = true;
        item = next ? next + 1 : NULL;
    }
    if (!saw_item) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid use group");
        return false;
    }
    return true;
}

static bool cxpr_document_lower_use(cxpr_model* model,
                                    const cxpr_document_ast_node* node,
                                    cxpr_error* err) {
    const char* text = cxpr_document_ast_node_text(node);
    char* owned;
    char* trimmed;
    bool ok;
    if (!text) return false;
    owned = cxpr_strdup(text);
    if (!owned) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    trimmed = cxpr_document_lower_trim_in_place(owned);
    if (*trimmed == '{') {
        ok = cxpr_document_lower_use_group(model, trimmed, err);
    } else if (cxpr_document_lower_find_keyword(trimmed, "from")) {
        ok = cxpr_document_lower_use_from_list(model, trimmed, err);
    } else {
        ok = cxpr_document_lower_use_clause(model, trimmed, err);
    }
    free(owned);
    return ok;
}

static bool cxpr_document_model_string_exists(char* const* values,
                                              size_t count,
                                              const char* value) {
    if (!value) return false;
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_document_names_match(values[i], value)) return true;
    }
    return false;
}

static bool cxpr_document_model_append_unique_string(char*** values,
                                                     size_t* count,
                                                     const char* value) {
    if (cxpr_document_model_string_exists(*values, *count, value)) return true;
    return cxpr_document_model_append_string(values, count, value);
}

static bool cxpr_document_model_append_name_list(char*** values,
                                                 size_t* count,
                                                 const char* names,
                                                 bool unique,
                                                 cxpr_error* err) {
    char* owned;
    char* cursor;
    char* save = NULL;
    if (!values || !count || !names) return false;
    owned = cxpr_strdup(names);
    if (!owned) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    for (cursor = cxpr_strtok_r(owned, ",", &save);
         cursor;
         cursor = cxpr_strtok_r(NULL, ",", &save)) {
        char* name = cxpr_document_lower_trim_in_place(cursor);
        if (!cxpr_document_lower_is_ident(name)) {
            free(owned);
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid name in list");
            return false;
        }
        if (!(unique ? cxpr_document_model_append_unique_string(values, count, name)
                     : cxpr_document_model_append_string(values, count, name))) {
            free(owned);
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
    }
    free(owned);
    return true;
}

static bool cxpr_document_model_append_struct_input_block(cxpr_model* model,
                                                          const cxpr_document_ast_node* node,
                                                          cxpr_error* err) {
    const char* root = cxpr_document_ast_node_name(node);
    if (!model || !root || !cxpr_document_lower_is_ident(root)) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid input struct name");
        return false;
    }
    for (size_t i = 0u; i < cxpr_document_ast_child_count(node); ++i) {
        const cxpr_document_ast_node* child = cxpr_document_ast_child(node, i);
        const char* field = cxpr_document_ast_node_name(child);
        size_t len;
        char* dotted;
        bool ok;
        if (!field || !cxpr_document_lower_is_dotted_ident(field)) {
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid input field name");
            return false;
        }
        len = strlen(root) + 1u + strlen(field);
        dotted = (char*)malloc(len + 1u);
        if (!dotted) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
        snprintf(dotted, len + 1u, "%s.%s", root, field);
        ok = cxpr_document_model_append_string(&model->inputs, &model->input_count, dotted);
        free(dotted);
        if (!ok) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
    }
    return true;
}

static bool cxpr_document_model_has_state(const cxpr_model* model, const char* name) {
    if (!model || !name) return false;
    for (size_t i = 0u; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
            cxpr_document_names_match(model->bindings[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool cxpr_document_model_append_constant(cxpr_model* model,
                                                const cxpr_document_ast_node* node) {
    cxpr_model_constant* grown;
    const char* name = cxpr_document_ast_node_name(node);
    const char* text = cxpr_document_ast_node_text(node);
    const cxpr_ast* expr = cxpr_document_ast_node_expression(node);
    if (!model || !name || !text || !expr) return false;
    grown = (cxpr_model_constant*)realloc(
        model->constants, (model->constant_count + 1u) * sizeof(*model->constants));
    if (!grown) return false;
    model->constants = grown;
    model->constants[model->constant_count] = (cxpr_model_constant){0};
    model->constants[model->constant_count].name = cxpr_strdup(name);
    model->constants[model->constant_count].source = cxpr_strdup(text);
    model->constants[model->constant_count].expr = cxpr_ast_clone(expr);
    model->constants[model->constant_count].span = cxpr_document_ast_node_span(node);
    model->constants[model->constant_count].has_span = true;
    if (!model->constants[model->constant_count].name ||
        !model->constants[model->constant_count].source ||
        !model->constants[model->constant_count].expr) {
        return false;
    }
    model->constant_count++;
    return true;
}

static bool cxpr_document_model_append_binding(cxpr_model* model,
                                               cxpr_model_binding_kind kind,
                                               const cxpr_document_ast_node* node) {
    cxpr_model_binding* grown;
    const char* name = cxpr_document_ast_node_name(node);
    const char* text = cxpr_document_ast_node_text(node);
    const cxpr_ast* expr = cxpr_document_ast_node_expression(node);
    if (!model || !name || !text || !expr) return false;
    grown = (cxpr_model_binding*)realloc(
        model->bindings, (model->binding_count + 1u) * sizeof(*model->bindings));
    if (!grown) return false;
    model->bindings = grown;
    model->bindings[model->binding_count] = (cxpr_model_binding){0};
    model->bindings[model->binding_count].kind = kind;
    model->bindings[model->binding_count].name = cxpr_strdup(name);
    model->bindings[model->binding_count].source = cxpr_strdup(text);
    model->bindings[model->binding_count].expr = cxpr_ast_clone(expr);
    model->bindings[model->binding_count].span = cxpr_document_ast_node_span(node);
    model->bindings[model->binding_count].has_span = true;
    if (!model->bindings[model->binding_count].name ||
        !model->bindings[model->binding_count].source ||
        !model->bindings[model->binding_count].expr) {
        return false;
    }
    model->binding_count++;
    return true;
}

static bool cxpr_document_model_append_anonymous_output(
    cxpr_model* model,
    const cxpr_document_ast_node* node) {
    cxpr_model_anonymous_output* grown;
    const char* text = cxpr_document_ast_node_text(node);
    const cxpr_ast* expr = cxpr_document_ast_node_expression(node);
    if (!model || !text || !expr) return false;
    grown = (cxpr_model_anonymous_output*)realloc(
        model->anonymous_outputs,
        (model->anonymous_output_count + 1u) * sizeof(*model->anonymous_outputs));
    if (!grown) return false;
    model->anonymous_outputs = grown;
    model->anonymous_outputs[model->anonymous_output_count] =
        (cxpr_model_anonymous_output){0};
    model->anonymous_outputs[model->anonymous_output_count].source = cxpr_strdup(text);
    model->anonymous_outputs[model->anonymous_output_count].expr = cxpr_ast_clone(expr);
    if (!model->anonymous_outputs[model->anonymous_output_count].source ||
        !model->anonymous_outputs[model->anonymous_output_count].expr) {
        return false;
    }
    model->anonymous_output_count++;
    return true;
}

static bool cxpr_document_model_append_record_function(
    cxpr_model* model,
    const cxpr_document_ast_node* node,
    cxpr_error* err);

static bool cxpr_document_model_append_function(cxpr_model* model,
                                                const cxpr_document_ast_node* node,
                                                cxpr_error* err) {
    const char* signature = cxpr_document_ast_node_name(node);
    const char* body = cxpr_document_ast_node_text(node);
    const char* body_start;
    char* def;
    size_t def_len;
    if (!model || !signature || !body) {
        return false;
    }
    body_start = body;
    while (*body_start && isspace((unsigned char)*body_start)) body_start++;
    if (!cxpr_document_ast_node_expression(node) ||
        *body_start == '{' ||
        cxpr_document_lower_has_top_level_comma(body)) {
        return cxpr_document_model_append_record_function(model, node, err);
    }
    def_len = strlen(signature) + strlen(body) + 5u;
    def = (char*)malloc(def_len);
    if (!def) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    snprintf(def, def_len, "%s => %s", signature, body);
    if (!cxpr_document_model_append_string(&model->functions, &model->function_count, def)) {
        free(def);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    free(def);
    return true;
}

static cxpr_ast* cxpr_document_lower_parse_expr(const char* text, cxpr_error* err) {
    cxpr_parser* parser;
    cxpr_ast* ast;
    if (!text || *text == '\0') {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Expected expression");
        return NULL;
    }
    parser = cxpr_parser_new();
    if (!parser) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    ast = cxpr_parse(parser, text, err);
    cxpr_parser_free(parser);
    return ast;
}

static void cxpr_document_lower_free_param_list(char** params, size_t count) {
    if (!params) return;
    for (size_t i = 0u; i < count; ++i) free(params[i]);
    free(params);
}

static bool cxpr_document_lower_parse_function_signature(const char* signature,
                                                         char** out_name,
                                                         char*** out_params,
                                                         size_t* out_param_count,
                                                         cxpr_error* err) {
    char* owned;
    char* name_start;
    char* open;
    char* close;
    char* args;
    char* save = NULL;
    char** params = NULL;
    size_t param_count = 0u;
    if (!signature || !out_name || !out_params || !out_param_count) return false;
    *out_name = NULL;
    *out_params = NULL;
    *out_param_count = 0u;
    owned = cxpr_strdup(signature);
    if (!owned) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    name_start = cxpr_document_lower_trim_in_place(owned);
    open = strchr(name_start, '(');
    close = strrchr(name_start, ')');
    if (!open || !close || close < open || close[1] != '\0') {
        free(owned);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX,
                                "Expected function signature: fn name(args)");
        return false;
    }
    *open = '\0';
    *close = '\0';
    name_start = cxpr_document_lower_trim_in_place(name_start);
    if (!cxpr_document_lower_is_ident(name_start)) {
        free(owned);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid function name");
        return false;
    }
    args = cxpr_document_lower_trim_in_place(open + 1);
    if (*args) {
        for (char* part = cxpr_strtok_r(args, ",", &save);
             part;
             part = cxpr_strtok_r(NULL, ",", &save)) {
            char** grown;
            char* param = cxpr_document_lower_trim_in_place(part);
            if (!cxpr_document_lower_is_ident(param)) {
                cxpr_document_lower_free_param_list(params, param_count);
                free(owned);
                cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid function parameter");
                return false;
            }
            grown = (char**)realloc(params, (param_count + 1u) * sizeof(*params));
            if (!grown) {
                cxpr_document_lower_free_param_list(params, param_count);
                free(owned);
                cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
                return false;
            }
            params = grown;
            params[param_count] = cxpr_strdup(param);
            if (!params[param_count]) {
                cxpr_document_lower_free_param_list(params, param_count);
                free(owned);
                cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
                return false;
            }
            param_count++;
        }
    }
    *out_name = cxpr_strdup(name_start);
    free(owned);
    if (!*out_name) {
        cxpr_document_lower_free_param_list(params, param_count);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    *out_params = params;
    *out_param_count = param_count;
    return true;
}

static bool cxpr_document_model_record_fields_append(cxpr_model_record_field** fields,
                                                     size_t* count,
                                                     const char* name,
                                                     const char* source,
                                                     cxpr_ast* expr) {
    cxpr_model_record_field* grown;
    grown = (cxpr_model_record_field*)realloc(
        *fields, (*count + 1u) * sizeof(**fields));
    if (!grown) return false;
    *fields = grown;
    (*fields)[*count] = (cxpr_model_record_field){0};
    (*fields)[*count].name = cxpr_strdup(name);
    (*fields)[*count].source = cxpr_strdup(source);
    (*fields)[*count].expr = expr;
    if (!(*fields)[*count].name || !(*fields)[*count].source) return false;
    (*count)++;
    return true;
}

static bool cxpr_document_lower_parse_record_return_fields(
    char* rest,
    const cxpr_alias* aliases,
    size_t alias_count,
    cxpr_model_record_field** out_fields,
    size_t* out_count,
    cxpr_error* err) {
    char* items;
    char* item_save = NULL;
    cxpr_model_record_field* fields = NULL;
    size_t field_count = 0u;
    if (!rest || !out_fields || !out_count) return false;
    *out_fields = NULL;
    *out_count = 0u;
    rest = cxpr_document_lower_trim_in_place(rest);
    if (*rest == '{') {
        char* close = strrchr(rest, '}');
        if (!close || close[1] != '\0') {
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX,
                                    "Expected record return: return { ... }");
            return false;
        }
        *close = '\0';
        items = cxpr_document_lower_trim_in_place(rest + 1);
    } else {
        items = rest;
    }
    for (char* item = cxpr_strtok_r(items, ",", &item_save);
         item;
         item = cxpr_strtok_r(NULL, ",", &item_save)) {
        char* eq;
        char* field_name;
        char* expr_text;
        cxpr_ast* field_ast;
        item = cxpr_document_lower_trim_in_place(item);
        if (*item == '\0') continue;
        eq = strchr(item, '=');
        if (eq) {
            *eq = '\0';
            field_name = cxpr_document_lower_trim_in_place(item);
            expr_text = cxpr_document_lower_trim_in_place(eq + 1);
        } else {
            field_name = item;
            expr_text = item;
        }
        if (!cxpr_document_lower_is_ident(field_name)) {
            cxpr_model_record_fields_free(fields, field_count);
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Invalid record return field");
            return false;
        }
        if (alias_count > 0u) {
            char* expanded = NULL;
            if (!cxpr_expand_aliases(expr_text, aliases, alias_count, &expanded, err)) {
                cxpr_model_record_fields_free(fields, field_count);
                return false;
            }
            field_ast = cxpr_document_lower_parse_expr(expanded, err);
            free(expanded);
        } else {
            field_ast = cxpr_document_lower_parse_expr(expr_text, err);
        }
        if (!field_ast) {
            cxpr_model_record_fields_free(fields, field_count);
            return false;
        }
        if (!cxpr_document_model_record_fields_append(
                &fields, &field_count, field_name, expr_text, field_ast)) {
            cxpr_ast_free(field_ast);
            cxpr_model_record_fields_free(fields, field_count);
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            return false;
        }
    }
    if (field_count == 0u) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Record return requires fields");
        return false;
    }
    *out_fields = fields;
    *out_count = field_count;
    return true;
}

static bool cxpr_document_model_append_record_function(
    cxpr_model* model,
    const cxpr_document_ast_node* node,
    cxpr_error* err) {
    cxpr_model_record_function fn = {0};
    char* body;
    cxpr_model_record_function* grown;
    if (!model || !node || !cxpr_document_ast_node_name(node) ||
        !cxpr_document_ast_node_text(node)) {
        return false;
    }
    if (!cxpr_document_lower_parse_function_signature(
            cxpr_document_ast_node_name(node),
            &fn.name,
            &fn.params,
            &fn.param_count,
            err)) {
        return false;
    }
    body = cxpr_strdup(cxpr_document_ast_node_text(node));
    if (!body) {
        cxpr_model_record_function_clear(&fn);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    if (!cxpr_document_lower_parse_record_return_fields(
            body, NULL, 0u, &fn.fields, &fn.field_count, err)) {
        free(body);
        cxpr_model_record_function_clear(&fn);
        return false;
    }
    free(body);
    grown = (cxpr_model_record_function*)realloc(
        model->record_functions,
        (model->record_function_count + 1u) * sizeof(*model->record_functions));
    if (!grown) {
        cxpr_model_record_function_clear(&fn);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }
    model->record_functions = grown;
    model->record_functions[model->record_function_count] = fn;
    model->record_function_count++;
    return true;
}

static void cxpr_document_lower_aliases_free(cxpr_alias* aliases, size_t count) {
    if (!aliases) return;
    for (size_t i = 0u; i < count; ++i) {
        free((char*)aliases[i].name);
        free((char*)aliases[i].expression);
    }
    free(aliases);
}

static bool cxpr_document_lower_aliases_append(cxpr_alias** aliases,
                                               size_t* count,
                                               const char* name,
                                               char* expression) {
    cxpr_alias* grown;
    if (!aliases || !count || !name || !expression) return false;
    grown = (cxpr_alias*)realloc(*aliases, (*count + 1u) * sizeof(**aliases));
    if (!grown) return false;
    *aliases = grown;
    (*aliases)[*count].name = cxpr_strdup(name);
    (*aliases)[*count].expression = expression;
    if (!(*aliases)[*count].name) {
        (*aliases)[*count].expression = NULL;
        return false;
    }
    (*count)++;
    return true;
}

static bool cxpr_document_model_append_block_function(
    cxpr_model* model,
    const cxpr_document_ast_node* node,
    cxpr_error* err) {
    const cxpr_document_ast_node* body;
    cxpr_alias* aliases = NULL;
    size_t alias_count = 0u;
    char* expanded_return = NULL;
    cxpr_model_record_field* record_fields = NULL;
    size_t record_field_count = 0u;
    char* def = NULL;
    bool ok = false;
    if (!model || !node || !cxpr_document_function_block_is_lowerable(node)) {
        return false;
    }
    body = cxpr_document_ast_child(node, 0u);
    for (size_t i = 0u; i < cxpr_document_ast_child_count(body); ++i) {
        const cxpr_document_ast_node* child = cxpr_document_ast_child(body, i);
        const char* text = cxpr_document_ast_node_text(child);
        char* expanded = NULL;
        if (cxpr_document_ast_node_kind(child) == CXPR_MODEL_AST_LOCAL_BINDING) {
            if (!cxpr_expand_aliases(text, aliases, alias_count, &expanded, err) ||
                !cxpr_document_lower_aliases_append(
                    &aliases, &alias_count, cxpr_document_ast_node_name(child), expanded)) {
                free(expanded);
                cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
                goto done;
            }
        } else if (cxpr_document_ast_node_kind(child) == CXPR_MODEL_AST_RETURN) {
            if (cxpr_document_ast_node_expression(child)) {
                if (!cxpr_expand_aliases(text, aliases, alias_count, &expanded_return, err)) {
                    goto done;
                }
            } else {
                char* record_body = cxpr_strdup(text);
                if (!record_body) {
                    cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
                    goto done;
                }
                if (!cxpr_document_lower_parse_record_return_fields(
                        record_body, aliases, alias_count,
                        &record_fields, &record_field_count, err)) {
                    free(record_body);
                    goto done;
                }
                free(record_body);
            }
        }
    }
    if (record_fields) {
        cxpr_model_record_function fn = {0};
        cxpr_model_record_function* grown;
        if (!cxpr_document_lower_parse_function_signature(
                cxpr_document_ast_node_name(node),
                &fn.name,
                &fn.params,
                &fn.param_count,
                err)) {
            goto done;
        }
        fn.fields = record_fields;
        fn.field_count = record_field_count;
        record_fields = NULL;
        record_field_count = 0u;
        grown = (cxpr_model_record_function*)realloc(
            model->record_functions,
            (model->record_function_count + 1u) * sizeof(*model->record_functions));
        if (!grown) {
            cxpr_model_record_function_clear(&fn);
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            goto done;
        }
        model->record_functions = grown;
        model->record_functions[model->record_function_count] = fn;
        model->record_function_count++;
        ok = true;
        goto done;
    }
    if (!expanded_return) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX,
                                "Function block requires a return statement");
        goto done;
    }
    {
        const char* signature = cxpr_document_ast_node_name(node);
        size_t def_len = strlen(signature) + strlen(expanded_return) + 5u;
        def = (char*)malloc(def_len);
        if (!def) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            goto done;
        }
        snprintf(def, def_len, "%s => %s", signature, expanded_return);
    }
    if (!cxpr_document_model_append_string(&model->functions, &model->function_count, def)) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        goto done;
    }
    ok = true;

done:
    free(def);
    free(expanded_return);
    cxpr_model_record_fields_free(record_fields, record_field_count);
    cxpr_document_lower_aliases_free(aliases, alias_count);
    return ok;
}

static bool cxpr_document_model_append_metadata(cxpr_model* model,
                                                const char* metadata_name,
                                                const char* target_name,
                                                cxpr_model_metadata_target_kind target_kind,
                                                const cxpr_document_ast_node* metadata_node) {
    cxpr_model_metadata* grown;
    const char* body = cxpr_document_ast_node_text(metadata_node);
    if (!model || !metadata_name || !target_name || !body) return false;
    grown = (cxpr_model_metadata*)realloc(
        model->metadatas, (model->metadata_count + 1u) * sizeof(*model->metadatas));
    if (!grown) return false;
    model->metadatas = grown;
    model->metadatas[model->metadata_count] = (cxpr_model_metadata){0};
    model->metadatas[model->metadata_count].name = cxpr_strdup(metadata_name);
    model->metadatas[model->metadata_count].body = cxpr_strdup(body);
    model->metadatas[model->metadata_count].target_kind = target_kind;
    model->metadatas[model->metadata_count].target_name = cxpr_strdup(target_name);
    model->metadatas[model->metadata_count].span = cxpr_document_ast_node_span(metadata_node);
    model->metadatas[model->metadata_count].has_span = true;
    if (!model->metadatas[model->metadata_count].name ||
        !model->metadatas[model->metadata_count].body ||
        !model->metadatas[model->metadata_count].target_name) {
        return false;
    }
    model->metadata_count++;
    return true;
}

static bool cxpr_document_model_append_host_block_fields(
    cxpr_model_host_block* block,
    const cxpr_document_ast_node* node);

static bool cxpr_document_model_append_host_block(cxpr_model* model,
                                                  const cxpr_document_ast_node* node) {
    cxpr_model_host_block* grown;
    cxpr_model_host_block* block;
    const char* kind = cxpr_document_ast_node_name(node);
    const char* name = cxpr_document_ast_node_value(node);
    const char* body = cxpr_document_ast_node_text(node);
    if (!model || !kind || !body) return false;
    grown = (cxpr_model_host_block*)realloc(
        model->host_blocks, (model->host_block_count + 1u) * sizeof(*model->host_blocks));
    if (!grown) return false;
    model->host_blocks = grown;
    block = &model->host_blocks[model->host_block_count];
    *block = (cxpr_model_host_block){0};
    block->kind = cxpr_strdup(kind);
    block->name = cxpr_strdup(name ? name : "");
    block->body = cxpr_strdup(body);
    block->span = cxpr_document_ast_node_span(node);
    block->has_span = true;
    if (!block->kind || !block->name || !block->body) {
        return false;
    }
    if (!cxpr_document_model_append_host_block_fields(block, node)) return false;
    model->host_block_count++;
    return true;
}

static bool cxpr_document_model_append_host_block_fields(
    cxpr_model_host_block* block,
    const cxpr_document_ast_node* node) {
    if (!block || !node) return false;
    for (size_t i = 0u; i < cxpr_document_ast_child_count(node); ++i) {
        const cxpr_document_ast_node* child = cxpr_document_ast_child(node, i);
        cxpr_model_host_block_field* fields;
        cxpr_model_host_block* children;
        cxpr_model_host_block* child_block;
        if (cxpr_document_ast_node_kind(child) == CXPR_DOCUMENT_AST_HOST_BLOCK) {
            const char* kind = cxpr_document_ast_node_name(child);
            const char* name = cxpr_document_ast_node_value(child);
            const char* body = cxpr_document_ast_node_text(child);
            if (!kind || !body) return false;
            children = (cxpr_model_host_block*)realloc(
                block->children, (block->child_count + 1u) * sizeof(*block->children));
            if (!children) return false;
            block->children = children;
            child_block = &block->children[block->child_count];
            *child_block = (cxpr_model_host_block){0};
            child_block->kind = cxpr_strdup(kind);
            child_block->name = cxpr_strdup(name ? name : "");
            child_block->body = cxpr_strdup(body);
            child_block->span = cxpr_document_ast_node_span(child);
            child_block->has_span = true;
            if (!child_block->kind || !child_block->name || !child_block->body ||
                !cxpr_document_model_append_host_block_fields(child_block, child)) {
                return false;
            }
            block->child_count++;
            continue;
        }
        if (cxpr_document_ast_node_kind(child) != CXPR_DOCUMENT_AST_HOST_FIELD) continue;
        fields = (cxpr_model_host_block_field*)realloc(
            block->fields, (block->field_count + 1u) * sizeof(*block->fields));
        if (!fields) return false;
        block->fields = fields;
        block->fields[block->field_count].key =
            cxpr_strdup(cxpr_document_ast_node_name(child));
        block->fields[block->field_count].value =
            cxpr_strdup(cxpr_document_ast_node_text(child));
        if (!block->fields[block->field_count].key ||
            !block->fields[block->field_count].value) {
            return false;
        }
        block->field_count++;
    }
    return true;
}

static bool cxpr_document_model_append_model_metadata_host_block(
    cxpr_model* model,
    const cxpr_document_ast_node* model_node,
    const cxpr_document_ast_node* metadata_node) {
    cxpr_model_host_block* grown;
    const char* name = cxpr_document_ast_node_name(model_node);
    const char* body = cxpr_document_ast_node_text(metadata_node);
    if (!model || !name || !body) return false;
    grown = (cxpr_model_host_block*)realloc(
        model->host_blocks, (model->host_block_count + 1u) * sizeof(*model->host_blocks));
    if (!grown) return false;
    model->host_blocks = grown;
    model->host_blocks[model->host_block_count] = (cxpr_model_host_block){0};
    model->host_blocks[model->host_block_count].kind = cxpr_strdup("model");
    model->host_blocks[model->host_block_count].name = cxpr_strdup(name);
    model->host_blocks[model->host_block_count].body = cxpr_strdup(body);
    model->host_blocks[model->host_block_count].span = cxpr_document_ast_node_span(model_node);
    model->host_blocks[model->host_block_count].has_span = true;
    if (!model->host_blocks[model->host_block_count].kind ||
        !model->host_blocks[model->host_block_count].name ||
        !model->host_blocks[model->host_block_count].body) {
        return false;
    }
    if (!cxpr_document_model_append_host_block_fields(
            &model->host_blocks[model->host_block_count], metadata_node)) {
        return false;
    }
    model->host_block_count++;
    return true;
}

static bool cxpr_document_lower_metadata_children(cxpr_model* model,
                                                  const cxpr_document_ast_node* owner,
                                                  const char* metadata_name,
                                                  cxpr_model_metadata_target_kind target_kind) {
    const char* target_name = cxpr_document_ast_node_name(owner);
    for (size_t i = 0u; i < cxpr_document_ast_child_count(owner); ++i) {
        const cxpr_document_ast_node* child = cxpr_document_ast_child(owner, i);
        if (cxpr_document_ast_node_kind(child) != CXPR_MODEL_AST_METADATA) continue;
        if (!cxpr_document_model_append_metadata(
                model, metadata_name, target_name ? target_name : "", target_kind, child)) {
            return false;
        }
    }
    return true;
}

static bool cxpr_document_lower_node_to_model(cxpr_model* model,
                                              const cxpr_document_ast_node* node,
                                              cxpr_error* err);

static bool cxpr_document_lower_children_to_model(cxpr_model* model,
                                                  const cxpr_document_ast_node* node,
                                                  cxpr_error* err) {
    for (size_t i = 0u; i < cxpr_document_ast_child_count(node); ++i) {
        if (!cxpr_document_lower_node_to_model(model, cxpr_document_ast_child(node, i), err)) {
            return false;
        }
    }
    return true;
}

static bool cxpr_document_lower_node_to_model(cxpr_model* model,
                                              const cxpr_document_ast_node* node,
                                              cxpr_error* err) {
    const char* name = cxpr_document_ast_node_name(node);
    switch (cxpr_document_ast_node_kind(node)) {
        case CXPR_DOCUMENT_AST_FILE:
            return cxpr_document_lower_children_to_model(model, node, err);
        case CXPR_DOCUMENT_AST_HOST_BLOCK:
            return cxpr_document_model_append_host_block(model, node);
        case CXPR_DOCUMENT_AST_MODEL_DECL:
            model->name = cxpr_strdup(name ? name : "");
            model->name_span = cxpr_document_ast_node_span(node);
            model->has_name_span = true;
            if (!model->name) return false;
            for (size_t i = 0u; i < cxpr_document_ast_child_count(node); ++i) {
                const cxpr_document_ast_node* child = cxpr_document_ast_child(node, i);
                if (cxpr_document_ast_node_kind(child) != CXPR_MODEL_AST_METADATA) continue;
                if (!cxpr_document_model_append_metadata(
                        model, "model", model->name, CXPR_MODEL_METADATA_TARGET_MODEL, child) ||
                    !cxpr_document_model_append_model_metadata_host_block(model, node, child)) {
                    return false;
                }
            }
            return true;
        case CXPR_MODEL_AST_USE:
            return cxpr_document_lower_use(model, node, err);
        case CXPR_MODEL_AST_FUNCTION_DECL:
            if (cxpr_document_ast_child_count(node) > 0u) {
                return cxpr_document_model_append_block_function(model, node, err);
            }
            return cxpr_document_model_append_function(model, node, err);
        case CXPR_MODEL_AST_INPUT_BLOCK:
            if (name && name[0] != '\0') {
                return cxpr_document_model_append_struct_input_block(model, node, err);
            }
            return cxpr_document_lower_children_to_model(model, node, err);
        case CXPR_MODEL_AST_PARAM_BLOCK:
        case CXPR_MODEL_AST_STATE_BLOCK:
        case CXPR_MODEL_AST_OUTPUT_BLOCK:
            return cxpr_document_lower_children_to_model(model, node, err);
        case CXPR_MODEL_AST_INPUT_DECL:
            return cxpr_document_model_append_name_list(
                &model->inputs, &model->input_count, name, false, err);
        case CXPR_MODEL_AST_PARAM_DECL:
            return cxpr_document_model_append_constant(model, node) &&
                   cxpr_document_lower_metadata_children(
                       model, node, "param", CXPR_MODEL_METADATA_TARGET_PARAM);
        case CXPR_MODEL_AST_STATE_DECL:
            return cxpr_document_model_append_binding(model, CXPR_MODEL_BINDING_STATE, node) &&
                   cxpr_document_lower_metadata_children(
                       model, node, "binding", CXPR_MODEL_METADATA_TARGET_STATE);
        case CXPR_MODEL_AST_STATE_UPDATE:
            return cxpr_document_model_append_binding(
                model, CXPR_MODEL_BINDING_STATE_UPDATE, node);
        case CXPR_MODEL_AST_INITIAL_STATE_UPDATE:
            return cxpr_document_lower_children_to_model(model, node, err) &&
                   cxpr_document_model_append_binding(
                       model, CXPR_MODEL_BINDING_STATE_UPDATE, node);
        case CXPR_MODEL_AST_BINDING:
            return cxpr_document_model_append_binding(model, CXPR_MODEL_BINDING_EXPR, node) &&
                   cxpr_document_lower_metadata_children(
                       model, node, "binding", CXPR_MODEL_METADATA_TARGET_BINDING);
        case CXPR_MODEL_AST_OUTPUT_STATE_UPDATE:
            return cxpr_document_model_append_binding(
                       model, CXPR_MODEL_BINDING_STATE_UPDATE, node) &&
                   cxpr_document_model_append_unique_string(
                       &model->outputs, &model->output_count, name);
        case CXPR_MODEL_AST_OUTPUT_DECL:
            if (cxpr_document_ast_node_expression(node) &&
                cxpr_document_model_has_state(model, name)) {
                cxpr_document_set_error(err, CXPR_ERR_SYNTAX,
                                        "State updates must use ':=' assignments");
                return false;
            }
            if (cxpr_document_ast_node_expression(node) &&
                !cxpr_document_model_append_binding(model, CXPR_MODEL_BINDING_EXPR, node)) {
                return false;
            }
            if (cxpr_document_ast_node_expression(node) ||
                cxpr_document_ast_child_count(node) > 0u) {
                return cxpr_document_model_append_unique_string(
                           &model->outputs, &model->output_count, name) &&
                       cxpr_document_lower_metadata_children(
                           model, node, "output", CXPR_MODEL_METADATA_TARGET_OUTPUT);
            }
            return cxpr_document_model_append_name_list(
                &model->outputs, &model->output_count, name, true, err);
        case CXPR_MODEL_AST_ANONYMOUS_OUTPUT:
            return cxpr_document_model_append_anonymous_output(model, node);
        case CXPR_DOCUMENT_AST_HOST_FIELD:
        case CXPR_MODEL_AST_METADATA:
        case CXPR_MODEL_AST_FUNCTION_BODY:
        case CXPR_MODEL_AST_LOCAL_BINDING:
        case CXPR_MODEL_AST_RETURN:
        default:
            cxpr_document_set_error(err, CXPR_ERR_SYNTAX,
                                    "Document AST node cannot be lowered in this context");
            return false;
    }
}

static cxpr_model* cxpr_document_lower_model_ast_direct(const cxpr_document_ast* syntax,
                                                        cxpr_error* err) {
    cxpr_model* model = (cxpr_model*)calloc(1u, sizeof(*model));
    if (!model) {
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    if (!cxpr_document_lower_node_to_model(model, cxpr_document_ast_root(syntax), err)) {
        if (err && err->code == CXPR_OK) {
            cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        }
        cxpr_model_free(model);
        return NULL;
    }
    return model;
}

cxpr_document* cxpr_parse_document(const char* source,
                                   unsigned extensions,
                                   cxpr_error* err) {
    cxpr_document_ast* syntax;
    cxpr_document* document;

    if (err) *err = (cxpr_error){0};
    syntax = cxpr_parse_document_ast(source, NULL, extensions, err);
    if (!syntax) return NULL;

    document = cxpr_lower_document_ast(syntax, err);
    cxpr_document_ast_free(syntax);
    return document;
}

cxpr_document* cxpr_lower_document_ast(const cxpr_document_ast* syntax, cxpr_error* err) {
    const char* source;
    const char* source_name;
    unsigned extensions;
    cxpr_model* model;
    cxpr_document_ast* syntax_copy;
    cxpr_document* document;

    if (err) *err = (cxpr_error){0};
    if (!syntax) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document syntax");
        return NULL;
    }
    source = cxpr_document_ast_source_text(syntax);
    source_name = cxpr_document_ast_source_name(syntax);
    extensions = cxpr_document_ast_extensions(syntax);

    model = cxpr_document_lower_model_ast_direct(syntax, err);
    if (!model) return NULL;

    if ((extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u &&
        !cxpr_document_model_constructs_empty(model, err)) {
        cxpr_model_free(model);
        return NULL;
    }
    cxpr_document_map_source_spans(model, syntax);

    syntax_copy = cxpr_parse_document_ast(source, source_name, extensions, err);
    if (!syntax_copy) {
        cxpr_model_free(model);
        return NULL;
    }
    document = (cxpr_document*)calloc(1u, sizeof(*document));
    if (!document) {
        cxpr_document_ast_free(syntax_copy);
        cxpr_model_free(model);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    document->model = model;
    document->syntax = syntax_copy;
    document->extensions = extensions;
    return document;
}

cxpr_document* cxpr_load_document_file(const char* path,
                                       unsigned extensions,
                                       cxpr_error* err) {
    FILE* file;
    long size;
    size_t read_size;
    char* source;
    cxpr_document* document;

    if (err) *err = (cxpr_error){0};
    if (!path) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document path");
        return NULL;
    }
    file = fopen(path, "rb");
    if (!file) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to open document");
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    size = ftell(file);
    if (size < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    source = (char*)malloc((size_t)size + 1u);
    if (!source) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    read_size = fread(source, 1u, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(source);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    source[size] = '\0';
    document = cxpr_parse_document(source, extensions, err);
    free(source);
    return document;
}

cxpr_document* cxpr_load_manifest_file(const char* path, cxpr_error* err) {
    return cxpr_load_document_file(path, CXPR_DOCUMENT_EXTENSION_NONE, err);
}

cxpr_document* cxpr_load_model_document_file(const char* path, cxpr_error* err) {
    return cxpr_load_document_file(path, CXPR_DOCUMENT_EXTENSION_MODEL, err);
}

cxpr_document* cxpr_parse_manifest(const char* source, cxpr_error* err) {
    return cxpr_parse_document(source, CXPR_DOCUMENT_EXTENSION_NONE, err);
}

cxpr_document* cxpr_parse_model_document(const char* source, cxpr_error* err) {
    return cxpr_parse_document(source, CXPR_DOCUMENT_EXTENSION_MODEL, err);
}

cxpr_model* cxpr_parse_model_source(const char* source, cxpr_error* err) {
    cxpr_document* document = cxpr_parse_model_document(source, err);
    cxpr_model* model;
    if (!document) return NULL;
    model = cxpr_document_take_model(document);
    cxpr_document_free(document);
    return model;
}

void cxpr_document_free(cxpr_document* document) {
    if (!document) return;
    cxpr_document_ast_free(document->syntax);
    cxpr_model_free(document->model);
    free(document);
}

cxpr_model* cxpr_document_take_model(cxpr_document* document) {
    cxpr_model* model;
    if (!document || (document->extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u) {
        return NULL;
    }
    model = document->model;
    document->model = NULL;
    return model;
}

const cxpr_document_ast* cxpr_document_syntax(const cxpr_document* document) {
    return document ? document->syntax : NULL;
}

size_t cxpr_document_host_block_count(const cxpr_document* document) {
    return document ? cxpr_model_host_block_count(document->model) : 0u;
}

const cxpr_model_host_block* cxpr_document_host_block_at(const cxpr_document* document,
                                                         size_t index) {
    return document ? cxpr_model_host_block_at(document->model, index) : NULL;
}

const cxpr_model_host_block* cxpr_document_host_block(const cxpr_document* document,
                                                      const char* kind) {
    if (!document || !kind) return NULL;
    for (size_t i = 0u; i < cxpr_model_host_block_count(document->model); ++i) {
        const cxpr_model_host_block* block = cxpr_model_host_block_at(document->model, i);
        const char* block_kind = cxpr_host_block_kind(block);
        if (block_kind && strcmp(block_kind, kind) == 0) return block;
    }
    return NULL;
}

bool cxpr_document_validate_host_blocks(const cxpr_document* document,
                                        const cxpr_host_block_registry* registry,
                                        cxpr_error* err) {
    if (!document) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document");
        return false;
    }
    return cxpr_model_validate_host_blocks(document->model, registry, err);
}

const cxpr_model* cxpr_document_model(const cxpr_document* document) {
    if (!document || (document->extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u) {
        return NULL;
    }
    return document->model;
}
