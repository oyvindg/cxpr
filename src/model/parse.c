#include "model/internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cxpr_ast* cxpr_model_parse_expr(const char* expr, size_t line, size_t column,
                                       cxpr_error* err);
static bool cxpr_model_parse_function_signature(char* header,
                                                size_t line_no,
                                                char** out_name,
                                                char*** out_params,
                                                size_t* out_param_count,
                                                cxpr_error* err);
static bool cxpr_model_parse_record_return_fields(char* rest,
                                                  const cxpr_model_local_binding* locals,
                                                  size_t local_count,
                                                  cxpr_model_record_field** out_fields,
                                                  size_t* out_count,
                                                  size_t line_no,
                                                  cxpr_error* err);
static bool cxpr_model_has_top_level_comma(const char* text);
static bool cxpr_model_statement_append(char** current, const char* text);

typedef struct {
    char* name;
    char* body;
} cxpr_model_pending_metadata;

static char* cxpr_model_substr(const char* start, size_t len) {
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static bool cxpr_model_append_text_len(char** current, const char* text, size_t len) {
    size_t old_len = *current ? strlen(*current) : 0u;
    char* next;
    if (!text) return true;
    next = (char*)realloc(*current, old_len + len + 2u);
    if (!next) return false;
    *current = next;
    if (old_len > 0u) (*current)[old_len++] = '\n';
    memcpy(*current + old_len, text, len);
    (*current)[old_len + len] = '\0';
    return true;
}

static bool cxpr_model_append_text(char** current, const char* text) {
    return cxpr_model_append_text_len(current, text, text ? strlen(text) : 0u);
}

static char* cxpr_model_trim_in_place(char* s) {
    char* end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    if (end > s && end[-1] == ';') {
        end[-1] = '\0';
        while (end > s && isspace((unsigned char)end[-2])) {
            end--;
            end[-1] = '\0';
        }
    }
    return s;
}

static bool cxpr_model_is_ident(const char* s) {
    if (!s || !(isalpha((unsigned char)*s) || *s == '_')) return false;
    s++;
    while (*s) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) return false;
        s++;
    }
    return true;
}

static bool cxpr_model_is_use_path(const char* s) {
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
static bool cxpr_model_string_exists(char* const* values, size_t count, const char* name) {
    for (size_t i = 0; i < count; ++i) {
        if (cxpr_model_names_match(values[i], name)) return true;
    }
    return false;
}

static bool cxpr_model_state_exists(const cxpr_model* model, const char* name) {
    if (!model) return false;
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
            cxpr_model_names_match(model->bindings[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool cxpr_model_keyword_line(const char* line, const char* keyword,
                                    const char** rest) {
    size_t n = strlen(keyword);
    if (strncmp(line, keyword, n) != 0) return false;
    if (line[n] != '\0' && !isspace((unsigned char)line[n])) return false;
    if (rest) {
        const char* r = line + n;
        while (*r && isspace((unsigned char)*r)) r++;
        *rest = r;
    }
    return true;
}

static bool cxpr_model_is_reserved_host_block_kind(const char* kind) {
    static const char* reserved[] = {
        "name", "use", "in", "fn", "update", "out", "state", "meta"
    };
    if (!kind) return true;
    for (size_t i = 0u; i < CXPR_ARRAY_COUNT(reserved); ++i) {
        if (strcmp(kind, reserved[i]) == 0) return true;
    }
    return false;
}

static bool cxpr_model_parse_host_block_start(const char* line,
                                              char** out_kind,
                                              char** out_name,
                                              const char** out_body_start,
                                              size_t line_no,
                                              cxpr_error* err) {
    const char* cursor = line;
    const char* kind_start;
    const char* kind_end;
    const char* name_start = NULL;
    const char* name_end = NULL;
    const char* open;
    const char* eq;

    *out_kind = NULL;
    *out_name = NULL;
    *out_body_start = NULL;
    if (!line || !(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
    open = strchr(line, '{');
    if (!open) return false;
    eq = strchr(line, '=');
    if (eq && eq < open) return false;

    kind_start = cursor;
    cursor++;
    while (*cursor && (isalnum((unsigned char)*cursor) || *cursor == '_')) cursor++;
    kind_end = cursor;
    *out_kind = cxpr_model_substr(kind_start, (size_t)(kind_end - kind_start));
    if (!*out_kind) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    if (cxpr_model_is_reserved_host_block_kind(*out_kind)) {
        free(*out_kind);
        *out_kind = NULL;
        if (strncmp(kind_start, "meta", 4u) == 0 && kind_end == kind_start + 4) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Legacy meta blocks are not supported", line_no, 1);
        }
        return false;
    }

    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (cursor < open) {
        name_start = cursor;
        if (!(isalpha((unsigned char)*cursor) || *cursor == '_')) {
            free(*out_kind);
            *out_kind = NULL;
            return false;
        }
        cursor++;
        while (*cursor && (isalnum((unsigned char)*cursor) || *cursor == '_' || *cursor == '-')) {
            cursor++;
        }
        name_end = cursor;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (cursor != open) {
            free(*out_kind);
            *out_kind = NULL;
            return false;
        }
    }

    *out_name = name_start ? cxpr_model_substr(name_start, (size_t)(name_end - name_start))
                           : cxpr_strdup("");
    if (!*out_name) {
        free(*out_kind);
        *out_kind = NULL;
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    *out_body_start = open + 1;
    return true;
}

static bool cxpr_model_host_block_line_uses_yaml_mapping(const char* line,
                                                         const char* end) {
    const char* cursor = line;
    const char* first_special = NULL;
    while (*cursor && (!end || cursor < end) && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0' || *cursor == '#') return false;
    for (const char* p = cursor; *p && (!end || p < end); ++p) {
        if (*p == ':' || *p == '=' || *p == '{') {
            first_special = p;
            break;
        }
    }
    return first_special && *first_special == ':';
}

static bool cxpr_model_host_block_body_uses_yaml_mapping(const char* body) {
    const char* line = body;
    while (line && *line) {
        const char* end = strchr(line, '\n');
        if (cxpr_model_host_block_line_uses_yaml_mapping(line, end)) return true;
        line = end ? end + 1 : NULL;
    }
    return false;
}

static bool cxpr_model_append_string(char*** values, size_t* count, const char* value) {
    char** next = (char**)realloc(*values, (*count + 1) * sizeof(char*));
    if (!next) return false;
    *values = next;
    (*values)[*count] = cxpr_strdup(value);
    if (!(*values)[*count]) return false;
    (*count)++;
    return true;
}

static bool cxpr_model_append_use(cxpr_model* model, const char* path, const char* alias) {
    char** next_uses;
    char** next_aliases;
    size_t next_count;
    if (!model || !path) return false;
    next_count = model->use_count + 1u;
    next_uses = (char**)realloc(model->uses, next_count * sizeof(char*));
    if (!next_uses) return false;
    model->uses = next_uses;
    next_aliases = (char**)realloc(model->use_aliases, next_count * sizeof(char*));
    if (!next_aliases) return false;
    model->use_aliases = next_aliases;
    model->uses[model->use_count] = cxpr_strdup(path);
    model->use_aliases[model->use_count] = alias ? cxpr_strdup(alias) : NULL;
    if (!model->uses[model->use_count] ||
        (alias && !model->use_aliases[model->use_count])) {
        free(model->uses[model->use_count]);
        free(model->use_aliases[model->use_count]);
        model->uses[model->use_count] = NULL;
        model->use_aliases[model->use_count] = NULL;
        return false;
    }
    model->use_count = next_count;
    return true;
}

static bool cxpr_model_parse_use_clause(char* text,
                                        const char** out_path,
                                        const char** out_alias) {
    char* cursor;
    char* as_kw;
    if (!text || !out_path || !out_alias) return false;
    *out_path = NULL;
    *out_alias = NULL;
    cursor = cxpr_model_trim_in_place(text);
    as_kw = strstr(cursor, " as ");
    if (as_kw) {
        char* alias;
        *as_kw = '\0';
        alias = cxpr_model_trim_in_place(as_kw + 4);
        cursor = cxpr_model_trim_in_place(cursor);
        if (!cxpr_model_is_ident(alias)) return false;
        *out_alias = alias;
    }
    if (!cxpr_model_is_use_path(cursor)) return false;
    *out_path = cursor;
    return true;
}

static bool cxpr_model_attach_metadatas(cxpr_model* model,
                                         const cxpr_model_pending_metadata* pending,
                                         size_t pending_count,
                                         cxpr_model_metadata_target_kind target_kind,
                                         const char* target_name) {
    cxpr_model_metadata* grown;
    if (!model || pending_count == 0u) return true;
    grown = (cxpr_model_metadata*)realloc(
        model->metadatas,
        (model->metadata_count + pending_count) * sizeof(cxpr_model_metadata));
    if (!grown) return false;
    model->metadatas = grown;
    for (size_t i = 0u; i < pending_count; ++i) {
        cxpr_model_metadata* out = &model->metadatas[model->metadata_count + i];
        out->name = cxpr_strdup(pending[i].name);
        out->body = cxpr_strdup(pending[i].body);
        out->target_kind = target_kind;
        out->target_name = cxpr_strdup(target_name ? target_name : "");
        if (!out->name || !out->body || !out->target_name) return false;
    }
    model->metadata_count += pending_count;
    return true;
}

static int cxpr_model_brace_delta(const char* text);

static char* cxpr_model_dup_trimmed(const char* start, size_t len) {
    const char* end;
    if (!start) return NULL;
    while (len > 0u && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    end = start + len;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    return cxpr_model_substr(start, (size_t)(end - start));
}

static bool cxpr_model_host_block_append_field(cxpr_model_host_block* block,
                                               const char* key_start,
                                               size_t key_len,
                                               const char* value_start,
                                               size_t value_len) {
    cxpr_model_host_block_field* grown;
    if (!block || !key_start || !value_start) return false;
    grown = (cxpr_model_host_block_field*)realloc(
        block->fields,
        (block->field_count + 1u) * sizeof(cxpr_model_host_block_field));
    if (!grown) return false;
    block->fields = grown;
    block->fields[block->field_count].key = cxpr_model_dup_trimmed(key_start, key_len);
    block->fields[block->field_count].value = cxpr_model_dup_trimmed(value_start, value_len);
    if (!block->fields[block->field_count].key ||
        !block->fields[block->field_count].value) {
        return false;
    }
    block->field_count++;
    return true;
}

static bool cxpr_model_host_block_append_child(cxpr_model_host_block* parent,
                                               const char* kind,
                                               const char* name,
                                               cxpr_model_host_block** out_child) {
    cxpr_model_host_block* grown;
    if (out_child) *out_child = NULL;
    if (!parent || !kind) return false;
    grown = (cxpr_model_host_block*)realloc(
        parent->children,
        (parent->child_count + 1u) * sizeof(cxpr_model_host_block));
    if (!grown) return false;
    parent->children = grown;
    memset(&parent->children[parent->child_count], 0, sizeof(cxpr_model_host_block));
    parent->children[parent->child_count].kind = cxpr_strdup(kind);
    parent->children[parent->child_count].name = cxpr_strdup(name ? name : "");
    parent->children[parent->child_count].body = cxpr_strdup("");
    if (!parent->children[parent->child_count].kind ||
        !parent->children[parent->child_count].name ||
        !parent->children[parent->child_count].body) {
        return false;
    }
    if (out_child) *out_child = &parent->children[parent->child_count];
    parent->child_count++;
    return true;
}

static bool cxpr_model_host_ident_start(char ch) {
    return isalpha((unsigned char)ch) || ch == '_';
}

static bool cxpr_model_host_ident_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-';
}

static const char* cxpr_model_host_skip_ws(const char* cursor) {
    while (cursor && *cursor) {
        while (isspace((unsigned char)*cursor) || *cursor == ',') cursor++;
        if (*cursor == '#') {
            while (*cursor && *cursor != '\n') cursor++;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            while (*cursor && *cursor != '\n') cursor++;
            continue;
        }
        break;
    }
    return cursor;
}

static const char* cxpr_model_host_parse_ident(const char* cursor,
                                               const char** out_start,
                                               size_t* out_len) {
    const char* start = cursor;
    if (out_start) *out_start = NULL;
    if (out_len) *out_len = 0u;
    if (!cursor || !cxpr_model_host_ident_start(*cursor)) return NULL;
    cursor++;
    while (cxpr_model_host_ident_char(*cursor)) cursor++;
    if (out_start) *out_start = start;
    if (out_len) *out_len = (size_t)(cursor - start);
    return cursor;
}

static const char* cxpr_model_host_scan_value_end(const char* cursor) {
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;
    char quote = '\0';

    while (cursor && *cursor) {
        if (quote) {
            if (*cursor == '\\' && cursor[1]) {
                cursor += 2;
                continue;
            }
            if (*cursor == quote) quote = '\0';
            cursor++;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            quote = *cursor++;
            continue;
        }
        if (*cursor == '(') paren_depth++;
        else if (*cursor == ')' && paren_depth > 0) paren_depth--;
        else if (*cursor == '[') bracket_depth++;
        else if (*cursor == ']' && bracket_depth > 0) bracket_depth--;
        else if (*cursor == '{') brace_depth++;
        else if (*cursor == '}' && brace_depth > 0) brace_depth--;
        else if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
            if (*cursor == ',' || *cursor == '\n' || *cursor == '}') return cursor;
        }
        cursor++;
    }
    return cursor;
}

static bool cxpr_model_host_value_contains_assignment(const char* start, size_t len) {
    const char* cursor = start;
    const char* end = start + len;
    char quote = '\0';
    int paren_depth = 0;
    int bracket_depth = 0;
    int brace_depth = 0;

    while (cursor < end) {
        if (quote) {
            if (*cursor == '\\' && cursor + 1 < end) {
                cursor += 2;
                continue;
            }
            if (*cursor == quote) quote = '\0';
            cursor++;
            continue;
        }
        if (*cursor == '"' || *cursor == '\'') {
            quote = *cursor++;
            continue;
        }
        if (*cursor == '(') paren_depth++;
        else if (*cursor == ')' && paren_depth > 0) paren_depth--;
        else if (*cursor == '[') bracket_depth++;
        else if (*cursor == ']' && bracket_depth > 0) bracket_depth--;
        else if (*cursor == '{') brace_depth++;
        else if (*cursor == '}' && brace_depth > 0) brace_depth--;
        else if (*cursor == '=' &&
                 paren_depth == 0 &&
                 bracket_depth == 0 &&
                 brace_depth == 0) {
            return true;
        }
        cursor++;
    }
    return false;
}

static bool cxpr_model_parse_host_block_items(cxpr_model_host_block* parent,
                                              const char** cursor,
                                              bool nested,
                                              size_t line_no,
                                              cxpr_error* err) {
    if (!parent || !cursor || !*cursor) return false;
    while (**cursor) {
        const char* key_start;
        const char* first_end;
        const char* probe;
        size_t key_len;

        *cursor = cxpr_model_host_skip_ws(*cursor);
        if (**cursor == '\0') return !nested;
        if (**cursor == '}') {
            (*cursor)++;
            return nested;
        }
        first_end = cxpr_model_host_parse_ident(*cursor, &key_start, &key_len);
        if (!first_end) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Host block body must contain fields or nested blocks",
                                 line_no, 1);
            return false;
        }
        probe = cxpr_model_host_skip_ws(first_end);
        if (*probe == '=') {
            const char* value_start = probe + 1;
            const char* value_end = cxpr_model_host_scan_value_end(value_start);
            if (cxpr_model_host_value_contains_assignment(
                    value_start,
                    (size_t)(value_end - value_start))) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Host block field value contains another assignment; use comma or newline",
                                     line_no, 1);
                return false;
            }
            if (!cxpr_model_host_block_append_field(
                    parent,
                    key_start,
                    key_len,
                    value_start,
                    (size_t)(value_end - value_start))) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            *cursor = value_end;
            continue;
        }
        {
            const char* name_start = NULL;
            size_t name_len = 0u;
            char* kind = NULL;
            char* name = NULL;
            cxpr_model_host_block* child = NULL;

            if (*probe != '{') {
                const char* after_name = cxpr_model_host_parse_ident(probe, &name_start, &name_len);
                if (!after_name) {
                    cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                         "Expected nested host block body", line_no, 1);
                    return false;
                }
                probe = cxpr_model_host_skip_ws(after_name);
            }
            if (*probe != '{') {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Expected nested host block body", line_no, 1);
                return false;
            }
            kind = cxpr_model_substr(key_start, key_len);
            name = name_start ? cxpr_model_substr(name_start, name_len) : cxpr_strdup("");
            if (!kind || !name ||
                !cxpr_model_host_block_append_child(parent, kind, name, &child)) {
                free(kind);
                free(name);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            free(kind);
            free(name);
            *cursor = probe + 1;
            if (!cxpr_model_parse_host_block_items(child, cursor, true, line_no, err)) {
                return false;
            }
        }
    }
    if (nested) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Unterminated nested host block", line_no, 1);
        return false;
    }
    return true;
}

static bool cxpr_model_parse_host_block_tree(cxpr_model_host_block* root,
                                             const char* body,
                                             size_t line_no,
                                             cxpr_error* err) {
    const char* cursor = body ? body : "";
    return cxpr_model_parse_host_block_items(root, &cursor, false, line_no, err);
}

static bool cxpr_model_append_host_block(cxpr_model* model,
                                         const char* kind,
                                         const char* name,
                                         const char* body,
                                         size_t line_no,
                                         cxpr_error* err) {
    cxpr_model_host_block* grown;
    if (!model || !kind) return false;
    if (cxpr_model_host_block_body_uses_yaml_mapping(body)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Host block body must use cxpr syntax, not YAML mapping syntax",
                             line_no, 1);
        return false;
    }
    grown = (cxpr_model_host_block*)realloc(
        model->host_blocks,
        (model->host_block_count + 1u) * sizeof(cxpr_model_host_block));
    if (!grown) return false;
    model->host_blocks = grown;
    memset(&model->host_blocks[model->host_block_count], 0, sizeof(cxpr_model_host_block));
    model->host_blocks[model->host_block_count].kind = cxpr_strdup(kind);
    model->host_blocks[model->host_block_count].name = cxpr_strdup(name ? name : "");
    model->host_blocks[model->host_block_count].body = cxpr_strdup(body ? body : "");
    if (!model->host_blocks[model->host_block_count].kind ||
        !model->host_blocks[model->host_block_count].name ||
        !model->host_blocks[model->host_block_count].body) {
        return false;
    }
    if (!cxpr_model_parse_host_block_tree(
            &model->host_blocks[model->host_block_count], body, line_no, err)) {
        return false;
    }
    model->host_block_count++;
    return true;
}

static bool cxpr_model_string_set_add(char*** values, size_t* count, const char* value) {
    if (cxpr_model_string_exists(*values, *count, value)) return true;
    return cxpr_model_append_string(values, count, value);
}

static bool cxpr_model_function_name_equals(const char* def, const char* name) {
    const char* open;
    size_t len;
    if (!def || !name) return false;
    open = strchr(def, '(');
    if (!open) return false;
    len = (size_t)(open - def);
    return strlen(name) == len && strncmp(def, name, len) == 0;
}

static bool cxpr_model_has_function_def(const cxpr_model* model, const char* name) {
    if (!model || !name) return false;
    for (size_t i = 0; i < model->function_count; ++i) {
        if (cxpr_model_function_name_equals(model->functions[i], name)) return true;
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        if (cxpr_model_names_match(model->record_functions[i].name, name)) return true;
    }
    return false;
}

static bool cxpr_model_collect_ast_functions(const cxpr_model* model, const cxpr_ast* ast,
                                             char*** names, size_t* count) {
    const char* used[256];
    size_t used_count = cxpr_ast_functions_used(ast, used, CXPR_ARRAY_COUNT(used));
    for (size_t i = 0; i < used_count && i < CXPR_ARRAY_COUNT(used); ++i) {
        if (cxpr_model_has_function_def(model, used[i])) continue;
        if (!cxpr_model_string_set_add(names, count, used[i])) return false;
    }
    return true;
}

static bool cxpr_model_collect_def_functions(const cxpr_model* model, const char* def,
                                             char*** names, size_t* count,
                                             cxpr_error* err) {
    const char* arrow = strstr(def, "=>");
    cxpr_ast* body;
    bool ok;
    if (!arrow) return true;
    arrow += 2;
    while (*arrow && isspace((unsigned char)*arrow)) arrow++;
    body = cxpr_model_parse_expr(arrow, 0, 0, err);
    if (!body) return false;
    ok = cxpr_model_collect_ast_functions(model, body, names, count);
    cxpr_ast_free(body);
    if (!ok) cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
    return ok;
}

bool cxpr_model_collect_required_defaults(const cxpr_model* model,
                                          char*** names,
                                          size_t* count,
                                          cxpr_error* err) {
    *names = NULL;
    *count = 0u;
    for (size_t i = 0; i < model->constant_count; ++i) {
        if (!cxpr_model_collect_ast_functions(model, model->constants[i].expr, names, count)) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (!cxpr_model_collect_ast_functions(model, model->bindings[i].expr, names, count)) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
            return false;
        }
    }
    for (size_t i = 0; i < model->function_count; ++i) {
        if (!cxpr_model_collect_def_functions(model, model->functions[i], names, count, err)) {
            return false;
        }
    }
    for (size_t i = 0; i < model->record_function_count; ++i) {
        for (size_t f = 0; f < model->record_functions[i].field_count; ++f) {
            if (!cxpr_model_collect_ast_functions(model,
                                                  model->record_functions[i].fields[f].expr,
                                                  names, count)) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
                return false;
            }
        }
    }
    return true;
}

static void cxpr_model_local_bindings_free(cxpr_model_local_binding* locals, size_t count) {
    if (!locals) return;
    for (size_t i = 0; i < count; ++i) {
        free(locals[i].name);
        cxpr_ast_free(locals[i].expr);
    }
    free(locals);
}

static bool cxpr_model_append_constant(cxpr_model* model, const char* name,
                                       const char* source, cxpr_ast* expr) {
    cxpr_model_constant* next =
        (cxpr_model_constant*)realloc(model->constants,
                                      (model->constant_count + 1) * sizeof(cxpr_model_constant));
    if (!next) return false;
    model->constants = next;
    model->constants[model->constant_count].name = cxpr_strdup(name);
    model->constants[model->constant_count].source = cxpr_strdup(source);
    model->constants[model->constant_count].expr = expr;
    if (!model->constants[model->constant_count].name ||
        !model->constants[model->constant_count].source) {
        return false;
    }
    model->constant_count++;
    return true;
}

static bool cxpr_model_append_binding(cxpr_model* model, cxpr_model_binding_kind kind,
                                      const char* name, const char* source,
                                      cxpr_ast* expr) {
    cxpr_model_binding* next =
        (cxpr_model_binding*)realloc(model->bindings,
                                     (model->binding_count + 1) * sizeof(cxpr_model_binding));
    if (!next) return false;
    model->bindings = next;
    model->bindings[model->binding_count].kind = kind;
    model->bindings[model->binding_count].name = cxpr_strdup(name);
    model->bindings[model->binding_count].source = cxpr_strdup(source);
    model->bindings[model->binding_count].expr = expr;
    if (!model->bindings[model->binding_count].name ||
        !model->bindings[model->binding_count].source) {
        return false;
    }
    model->binding_count++;
    return true;
}

static bool cxpr_model_append_record_function(cxpr_model* model,
                                              cxpr_model_record_function* fn) {
    cxpr_model_record_function* next =
        (cxpr_model_record_function*)realloc(
            model->record_functions,
            (model->record_function_count + 1u) * sizeof(cxpr_model_record_function));
    if (!next) return false;
    model->record_functions = next;
    model->record_functions[model->record_function_count] = *fn;
    memset(fn, 0, sizeof(*fn));
    model->record_function_count++;
    return true;
}

static cxpr_ast* cxpr_model_parse_expr(const char* expr, size_t line, size_t column,
                                       cxpr_error* err) {
    cxpr_parser* parser;
    cxpr_ast* ast;
    cxpr_error inner = {0};

    if (!expr || *expr == '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected expression", line, column);
        return NULL;
    }

    parser = cxpr_parser_new();
    if (!parser) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line, column);
        return NULL;
    }

    ast = cxpr_parse(parser, expr, &inner);
    cxpr_parser_free(parser);
    if (!ast) {
        if (err) {
            *err = inner;
            err->line = line + (inner.line > 0 ? inner.line - 1 : 0);
            err->column = column + (inner.column > 0 ? inner.column - 1 : 0);
        }
        return NULL;
    }
    return ast;
}

static bool cxpr_model_parse_input_list(cxpr_model* model, const char* rest,
                                        size_t line_no, cxpr_error* err) {
    const char* open = strchr(rest, '{');
    const char* close = strrchr(rest, '}');
    char* list;
    char* cursor;
    char* save = NULL;

    if (open || close) {
        if (!open || !close || close < open) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected input list: in { ... }",
                                 line_no, 1);
            return false;
        }
        list = cxpr_model_substr(open + 1, (size_t)(close - open - 1));
    } else {
        list = cxpr_strdup(rest);
    }

    if (!list) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }

    for (cursor = cxpr_strtok_r(list, ",", &save); cursor;
         cursor = cxpr_strtok_r(NULL, ",", &save)) {
        char* name = cxpr_model_trim_in_place(cursor);
        if (!cxpr_model_is_ident(name)) {
            free(list);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid input name", line_no, 1);
            return false;
        }
        if (!cxpr_model_append_string(&model->inputs, &model->input_count, name)) {
            free(list);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
    }

    free(list);
    return true;
}

static bool cxpr_model_parse_name_list(char*** values, size_t* count, const char* rest,
                                       const char* error_message,
                                       size_t line_no, cxpr_error* err) {
    const char* open = strchr(rest, '{');
    const char* close = strrchr(rest, '}');
    char* list;
    char* cursor;
    char* save = NULL;

    if (open || close) {
        if (!open || !close || close < open) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, error_message, line_no, 1);
            return false;
        }
        list = cxpr_model_substr(open + 1, (size_t)(close - open - 1));
    } else {
        list = cxpr_strdup(rest);
    }

    if (!list) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }

    for (cursor = cxpr_strtok_r(list, ",", &save); cursor;
         cursor = cxpr_strtok_r(NULL, ",", &save)) {
        char* name = cxpr_model_trim_in_place(cursor);
        if (!cxpr_model_is_ident(name)) {
            free(list);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid name in list", line_no, 1);
            return false;
        }
        if (!cxpr_model_string_set_add(values, count, name)) {
            free(list);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
    }

    free(list);
    return true;
}

static bool cxpr_model_parse_assignment(cxpr_model* model, char* line,
                                        cxpr_model_binding_kind kind,
                                        size_t line_no, bool constant,
                                        cxpr_error* err) {
    char* eq = strchr(line, '=');
    char* name;
    char* expr_text;
    char* metadata_body = NULL;
    cxpr_ast* expr;
    size_t expr_col;

    if (!eq) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected '='", line_no, 1);
        return false;
    }

    *eq = '\0';
    name = cxpr_model_trim_in_place(line);
    expr_text = cxpr_model_trim_in_place(eq + 1);
    expr_col = (size_t)(expr_text - line) + 1;

    {
        char* open = strchr(expr_text, '{');
        char* close = strrchr(expr_text, '}');
        if (open || close) {
            if (!open || !close || close < open || close[1] != '\0') {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Expected metadata block after assignment",
                                     line_no, 1);
                return false;
            }
            *open = '\0';
            *close = '\0';
            metadata_body = open + 1;
            expr_text = cxpr_model_trim_in_place(expr_text);
        }
    }

    if (constant && name[0] == '$') name++;
    if (!cxpr_model_is_ident(name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid symbol name", line_no, 1);
        return false;
    }

    expr = cxpr_model_parse_expr(expr_text, line_no, expr_col, err);
    if (!expr) return false;

    if (constant) {
        if (!cxpr_model_append_constant(model, name, expr_text, expr)) {
            cxpr_ast_free(expr);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
    } else if (!cxpr_model_append_binding(model, kind, name, expr_text, expr)) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }

    if (metadata_body) {
        cxpr_model_pending_metadata pending = {0};
        pending.name = cxpr_strdup(constant ? "param" : "binding");
        pending.body = cxpr_strdup(metadata_body);
        if (!pending.name || !pending.body ||
            !cxpr_model_attach_metadatas(
                model,
                &pending,
                1u,
                constant ? CXPR_MODEL_METADATA_TARGET_PARAM
                         : (kind == CXPR_MODEL_BINDING_STATE
                                ? CXPR_MODEL_METADATA_TARGET_STATE
                                : CXPR_MODEL_METADATA_TARGET_BINDING),
                name)) {
            free(pending.name);
            free(pending.body);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
        free(pending.name);
        free(pending.body);
    }

    return true;
}

static bool cxpr_model_parse_param_block_entry(cxpr_model* model,
                                               const char* start,
                                               size_t len,
                                               size_t line_no,
                                               cxpr_error* err) {
    char* entry;
    char* trimmed;
    bool ok;

    while (len > 0u && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len > 0u && isspace((unsigned char)start[len - 1u])) len--;
    if (len == 0u) return true;

    entry = cxpr_model_substr(start, len);
    if (!entry) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    trimmed = cxpr_model_trim_in_place(entry);
    ok = cxpr_model_parse_assignment(model, trimmed, CXPR_MODEL_BINDING_EXPR,
                                     line_no, true, err);
    free(entry);
    return ok;
}

static bool cxpr_model_parse_param_block(cxpr_model* model,
                                         char* statement,
                                         size_t line_no,
                                         cxpr_error* err) {
    char* after_dollar;
    char* open;
    char* close;
    char* body_start;
    const char* entry_start;
    const char* cursor;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;

    if (!model || !statement || statement[0] != '$') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected param block: ${ ... }", line_no, 1);
        return false;
    }
    after_dollar = statement + 1;
    while (*after_dollar && isspace((unsigned char)*after_dollar)) after_dollar++;
    if (*after_dollar != '{') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected param block: ${ ... }", line_no, 1);
        return false;
    }
    open = after_dollar;
    close = strrchr(statement, '}');
    if (!open || !close || close < open) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected param block: ${ ... }", line_no, 1);
        return false;
    }
    if (*cxpr_model_trim_in_place(close + 1) != '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Unexpected text after param block", line_no, 1);
        return false;
    }

    *close = '\0';
    body_start = open + 1;
    entry_start = body_start;
    for (cursor = body_start; ; ++cursor) {
        char ch = *cursor;
        bool at_end = ch == '\0';
        bool separator = false;

        if (!at_end) {
            switch (ch) {
            case '(':
                paren_depth++;
                break;
            case ')':
                if (paren_depth > 0) paren_depth--;
                break;
            case '{':
                brace_depth++;
                break;
            case '}':
                if (brace_depth > 0) brace_depth--;
                break;
            case '[':
                bracket_depth++;
                break;
            case ']':
                if (bracket_depth > 0) bracket_depth--;
                break;
            case ',':
            case '\n':
                separator = paren_depth == 0 && brace_depth == 0 && bracket_depth == 0;
                break;
            default:
                break;
            }
        }

        if (at_end || separator) {
            if (!cxpr_model_parse_param_block_entry(
                    model, entry_start, (size_t)(cursor - entry_start), line_no, err)) {
                return false;
            }
            if (at_end) break;
            entry_start = cursor + 1;
        }
    }
    return true;
}

static bool cxpr_model_parse_state_update_assignment(cxpr_model* model, char* line,
                                                     size_t line_no, cxpr_error* err) {
    char* op = strstr(line, ":=");
    char* name;
    char* expr_text;
    cxpr_ast* expr;
    if (!op) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected ':=' in state update",
                             line_no, 1);
        return false;
    }
    *op = '\0';
    name = cxpr_model_trim_in_place(line);
    if (!cxpr_model_is_ident(name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid state update name",
                             line_no, 1);
        return false;
    }
    if (!cxpr_model_state_exists(model, name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "State update references unknown state", line_no, 1);
        return false;
    }
    expr_text = cxpr_model_trim_in_place(op + 2);
    expr = cxpr_model_parse_expr(expr_text, line_no, (size_t)(expr_text - line) + 1u, err);
    if (!expr) return false;
    if (!cxpr_model_append_binding(model, CXPR_MODEL_BINDING_STATE_UPDATE,
                                   name, expr_text, expr)) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    return true;
}

static bool cxpr_model_parse_state_assignment(cxpr_model* model, char* line,
                                              size_t line_no, cxpr_error* err) {
    char* eq = strchr(line, '=');
    char* name;
    char* expr_text;
    cxpr_ast* expr;
    if (!eq) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected '=' in state", line_no, 1);
        return false;
    }
    *eq = '\0';
    name = cxpr_model_trim_in_place(line);
    expr_text = cxpr_model_trim_in_place(eq + 1);
    if (!cxpr_model_is_ident(name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid state name", line_no, 1);
        return false;
    }
    expr = cxpr_model_parse_expr(expr_text, line_no, 1, err);
    if (!expr) return false;
    if (!cxpr_model_append_binding(model, CXPR_MODEL_BINDING_STATE,
                                   name, expr_text, expr)) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    return true;
}

static bool cxpr_model_line_starts_assignment(const char* line) {
    char* owned;
    char* trimmed;
    char* eq;
    bool ok;

    if (!line) return false;
    owned = cxpr_strdup(line);
    if (!owned) return false;
    trimmed = cxpr_model_trim_in_place(owned);
    eq = strchr(trimmed, '=');
    if (!eq) {
        free(owned);
        return false;
    }
    *eq = '\0';
    ok = cxpr_model_is_ident(cxpr_model_trim_in_place(trimmed));
    free(owned);
    return ok;
}

static bool cxpr_model_append_update_local(cxpr_model_local_binding** locals,
                                           size_t* local_count,
                                           cxpr_model* model,
                                           const char* name,
                                           cxpr_ast* expr,
                                           const char* source,
                                           size_t line_no,
                                           cxpr_error* err) {
    cxpr_model_local_binding* grown;
    bool appended;
    if (cxpr_model_local_lookup(*locals, *local_count, name)) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Duplicate update state local", line_no, 1);
        return false;
    }
    appended = cxpr_model_append_binding(model, CXPR_MODEL_BINDING_LOCAL,
                                         name, source, expr);
    if (!appended) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    grown = (cxpr_model_local_binding*)realloc(
        *locals, (*local_count + 1u) * sizeof(cxpr_model_local_binding));
    if (!grown) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    *locals = grown;
    (*locals)[*local_count].name = cxpr_strdup(name);
    (*locals)[*local_count].expr = NULL;
    if (!(*locals)[*local_count].name) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    (*local_count)++;
    return true;
}

static bool cxpr_model_parse_update_state_statement(cxpr_model* model,
                                                    char* statement,
                                                    cxpr_model_local_binding** locals,
                                                    size_t* local_count,
                                                    size_t line_no,
                                                    cxpr_error* err) {
    char* eq = strchr(statement, '=');
    char* name;
    char* expr_text;
    cxpr_ast* expr;
    bool ok;

    if (!eq) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected '=' in state update",
                             line_no, 1);
        return false;
    }
    *eq = '\0';
    name = cxpr_model_trim_in_place(statement);
    expr_text = cxpr_model_trim_in_place(eq + 1);
    if (!cxpr_model_is_ident(name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid state update name",
                             line_no, 1);
        return false;
    }

    expr = cxpr_model_parse_expr(expr_text, line_no, 1, err);
    if (!expr) return false;

    if (cxpr_model_state_exists(model, name)) {
        ok = cxpr_model_append_binding(model, CXPR_MODEL_BINDING_STATE_UPDATE,
                                       name, expr_text, expr);
        if (!ok) {
            cxpr_ast_free(expr);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
        return true;
    }

    return cxpr_model_append_update_local(locals, local_count, model, name, expr,
                                          expr_text, line_no, err);
}

static bool cxpr_model_parse_update_state_block(cxpr_model* model,
                                                char* body,
                                                size_t line_no,
                                                cxpr_error* err) {
    char* cursor;
    char* save = NULL;
    char* current = NULL;
    cxpr_model_local_binding* locals = NULL;
    size_t local_count = 0u;
    bool ok = true;

    for (cursor = cxpr_strtok_r(body, "\n", &save); cursor;
         cursor = cxpr_strtok_r(NULL, "\n", &save)) {
        char* trimmed = cxpr_model_trim_in_place(cursor);
        if (*trimmed == '\0' || *trimmed == '#') continue;
        if (current && cxpr_model_line_starts_assignment(trimmed)) {
            if (!cxpr_model_parse_update_state_statement(model, current, &locals,
                                                         &local_count, line_no, err)) {
                ok = false;
                goto done;
            }
            free(current);
            current = NULL;
        }
        if (!cxpr_model_statement_append(&current, trimmed)) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            ok = false;
            goto done;
        }
    }

    if (current && !cxpr_model_parse_update_state_statement(model, current, &locals,
                                                            &local_count, line_no, err)) {
        ok = false;
    }

done:
    free(current);
    cxpr_model_local_bindings_free(locals, local_count);
    return ok;
}

static bool cxpr_model_parse_assignment_block(cxpr_model* model,
                                              char* rest,
                                              bool state_update,
                                              size_t line_no,
                                              cxpr_error* err) {
    char* open = strchr(rest, '{');
    char* close = strrchr(rest, '}');
    char* body;
    char* cursor;
    char* save = NULL;

    if (!open || !close || close < open) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             state_update
                                 ? "State updates must use ':=' assignments"
                                 : "Expected state block: state { ... }",
                             line_no, 1);
        return false;
    }
    *open = '\0';
    if (*cxpr_model_trim_in_place(rest) != '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             state_update
                                 ? "State updates must use ':=' assignments"
                                 : "Expected state block: state { ... }",
                             line_no, 1);
        return false;
    }
    *close = '\0';
    if (*cxpr_model_trim_in_place(close + 1) != '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unexpected text after state block",
                             line_no, 1);
        return false;
    }

    body = open + 1;
    if (state_update) return cxpr_model_parse_update_state_block(model, body, line_no, err);

    for (cursor = cxpr_strtok_r(body, "\n", &save); cursor;
         cursor = cxpr_strtok_r(NULL, "\n", &save)) {
        char* line = cxpr_model_trim_in_place(cursor);
        if (*line == '\0' || *line == '#') continue;
        if (!cxpr_model_parse_state_assignment(model, line, line_no, err)) {
            return false;
        }
    }

    return true;
}

static bool cxpr_model_parse_out_assignment(cxpr_model* model, char* line,
                                            size_t line_no, cxpr_error* err) {
    char* name_copy = cxpr_strdup(line);
    char* staged_op = name_copy ? strstr(name_copy, ":=") : NULL;
    char* eq = name_copy ? strchr(name_copy, '=') : NULL;
    char* name;
    bool is_state;
    bool ok;

    if (!name_copy) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    if (!staged_op && !eq) {
        free(name_copy);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected '=' in out assignment",
                             line_no, 1);
        return false;
    }

    if (staged_op) {
        *staged_op = '\0';
    } else {
        *eq = '\0';
    }
    name = cxpr_model_trim_in_place(name_copy);
    if (!cxpr_model_is_ident(name)) {
        free(name_copy);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid output name", line_no, 1);
        return false;
    }

    is_state = cxpr_model_state_exists(model, name);
    if (staged_op) {
        ok = cxpr_model_parse_state_update_assignment(model, line, line_no, err);
        if (ok && !cxpr_model_string_set_add(&model->outputs, &model->output_count, name)) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            ok = false;
        }
        free(name_copy);
        return ok;
    }

    if (is_state) {
        free(name_copy);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "State updates must use ':=' assignments", line_no, 1);
        return false;
    }

    ok = cxpr_model_parse_assignment(model, line, CXPR_MODEL_BINDING_EXPR,
                                     line_no, false, err);
    if (ok && !cxpr_model_string_set_add(&model->outputs, &model->output_count, name)) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        ok = false;
    }
    if (!ok && err && err->code == CXPR_OK) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid out assignment",
                             line_no, 1);
    }
    free(name_copy);
    return ok;
}

static bool cxpr_model_append_anonymous_output(cxpr_model* model,
                                               const char* source,
                                               cxpr_ast* expr) {
    cxpr_model_anonymous_output* grown;
    if (!model || !source || !expr) return false;
    grown = (cxpr_model_anonymous_output*)realloc(
        model->anonymous_outputs,
        (model->anonymous_output_count + 1u) * sizeof(*model->anonymous_outputs));
    if (!grown) return false;
    model->anonymous_outputs = grown;
    model->anonymous_outputs[model->anonymous_output_count].source = cxpr_strdup(source);
    model->anonymous_outputs[model->anonymous_output_count].expr = expr;
    if (!model->anonymous_outputs[model->anonymous_output_count].source) {
        model->anonymous_outputs[model->anonymous_output_count].expr = NULL;
        return false;
    }
    model->anonymous_output_count++;
    return true;
}

static bool cxpr_model_parse_anonymous_out(cxpr_model* model,
                                           const char* rest,
                                           size_t line_no,
                                           cxpr_error* err) {
    cxpr_ast* expr = cxpr_model_parse_expr(rest, line_no, 1, err);
    if (!expr) return false;
    if (expr->type != CXPR_NODE_FUNCTION_CALL) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Anonymous out must be a record function call",
                             line_no, 1);
        return false;
    }
    if (!cxpr_model_append_anonymous_output(model, rest, expr)) {
        cxpr_ast_free(expr);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    return true;
}

static bool cxpr_model_parse_out_metadata_block(cxpr_model* model,
                                                char* rest,
                                                size_t line_no,
                                                cxpr_error* err) {
    char* open = strchr(rest, '{');
    char* close = strrchr(rest, '}');
    char* name;
    char* body;
    cxpr_model_pending_metadata pending = {0};
    bool ok = false;

    if (!open || !close || close < open) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected output metadata block: out name { ... }",
                             line_no, 1);
        return false;
    }
    *open = '\0';
    *close = '\0';
    name = cxpr_model_trim_in_place(rest);
    body = open + 1;
    if (!cxpr_model_is_ident(name)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid output name", line_no, 1);
        return false;
    }
    if (!cxpr_model_string_set_add(&model->outputs, &model->output_count, name)) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }

    pending.name = cxpr_strdup("output");
    pending.body = cxpr_strdup(body);
    if (!pending.name || !pending.body) {
        free(pending.name);
        free(pending.body);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    ok = cxpr_model_attach_metadatas(model, &pending, 1u,
                                      CXPR_MODEL_METADATA_TARGET_OUTPUT, name);
    free(pending.name);
    free(pending.body);
    if (!ok) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
    }
    return ok;
}

static bool cxpr_model_parse_function(cxpr_model* model, char* statement,
                                      size_t line_no, cxpr_error* err) {
    char* eq = strchr(statement, '=');
    char* lhs;
    char* rhs;
    char* name_start;
    char* open;
    char* close;
    size_t def_len;
    char* def;

    if (!eq) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected '=' in function", line_no, 1);
        return false;
    }

    *eq = '\0';
    lhs = cxpr_model_trim_in_place(statement);
    rhs = cxpr_model_trim_in_place(eq + 1);
    if (strncmp(lhs, "fn", 2) != 0 || !isspace((unsigned char)lhs[2])) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid function declaration", line_no, 1);
        return false;
    }
    name_start = cxpr_model_trim_in_place(lhs + 2);
    open = strchr(name_start, '(');
    close = strrchr(name_start, ')');
    if (!open || !close || close < open || close[1] != '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected function signature: fn name(args) = expr",
                             line_no, 1);
        return false;
    }

    *open = '\0';
    if (!cxpr_model_is_ident(cxpr_model_trim_in_place(name_start))) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid function name", line_no, 1);
        return false;
    }
    *open = '(';
    if (!rhs || *rhs == '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected function body", line_no, 1);
        return false;
    }

    if (*rhs == '{' || cxpr_model_has_top_level_comma(rhs)) {
        cxpr_model_record_function fn = {0};
        char* header = cxpr_strdup(lhs);
        if (!header) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
        if (!cxpr_model_parse_function_signature(header, line_no, &fn.name,
                                                 &fn.params, &fn.param_count, err)) {
            free(header);
            return false;
        }
        free(header);
        if (!cxpr_model_parse_record_return_fields(rhs, NULL, 0u,
                                                  &fn.fields, &fn.field_count,
                                                  line_no, err)) {
            cxpr_model_record_function_clear(&fn);
            return false;
        }
        if (!cxpr_model_append_record_function(model, &fn)) {
            cxpr_model_record_function_clear(&fn);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
        return true;
    }

    def_len = strlen(name_start) + strlen(rhs) + 5u;
    def = (char*)malloc(def_len);
    if (!def) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    snprintf(def, def_len, "%s => %s", name_start, rhs);
    if (!cxpr_model_append_string(&model->functions, &model->function_count, def)) {
        free(def);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    free(def);
    return true;
}

static void cxpr_model_free_param_list(char** params, size_t count) {
    if (!params) return;
    for (size_t i = 0; i < count; ++i) free(params[i]);
    free(params);
}

static bool cxpr_model_parse_function_signature(char* header,
                                                size_t line_no,
                                                char** out_name,
                                                char*** out_params,
                                                size_t* out_param_count,
                                                cxpr_error* err) {
    char* name_start;
    char* open;
    char* close;
    char* args;
    char* save = NULL;
    char** params = NULL;
    size_t param_count = 0u;

    if (!header || !out_name || !out_params || !out_param_count) return false;
    *out_name = NULL;
    *out_params = NULL;
    *out_param_count = 0u;
    if (strncmp(header, "fn", 2) != 0 || !isspace((unsigned char)header[2])) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid function declaration", line_no, 1);
        return false;
    }

    name_start = cxpr_model_trim_in_place(header + 2);
    open = strchr(name_start, '(');
    close = strrchr(name_start, ')');
    if (!open || !close || close < open || close[1] != '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Expected function signature: fn name(args)",
                             line_no, 1);
        return false;
    }

    *open = '\0';
    *close = '\0';
    name_start = cxpr_model_trim_in_place(name_start);
    if (!cxpr_model_is_ident(name_start)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid function name", line_no, 1);
        return false;
    }

    args = cxpr_model_trim_in_place(open + 1);
    if (*args) {
        for (char* part = cxpr_strtok_r(args, ",", &save);
             part;
             part = cxpr_strtok_r(NULL, ",", &save)) {
            char** grown;
            char* param = cxpr_model_trim_in_place(part);
            if (!cxpr_model_is_ident(param)) {
                cxpr_model_free_param_list(params, param_count);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid function parameter", line_no, 1);
                return false;
            }
            grown = (char**)realloc(params, (param_count + 1u) * sizeof(char*));
            if (!grown) {
                cxpr_model_free_param_list(params, param_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            params = grown;
            params[param_count] = cxpr_strdup(param);
            if (!params[param_count]) {
                cxpr_model_free_param_list(params, param_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            param_count++;
        }
    }

    *out_name = cxpr_strdup(name_start);
    if (!*out_name) {
        cxpr_model_free_param_list(params, param_count);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    *out_params = params;
    *out_param_count = param_count;
    return true;
}

static bool cxpr_model_record_fields_append(cxpr_model_record_field** fields,
                                            size_t* count,
                                            const char* name,
                                            const char* source,
                                            cxpr_ast* expr) {
    cxpr_model_record_field* grown =
        (cxpr_model_record_field*)realloc(*fields,
                                          (*count + 1u) * sizeof(cxpr_model_record_field));
    if (!grown) return false;
    *fields = grown;
    (*fields)[*count].name = cxpr_strdup(name);
    (*fields)[*count].source = cxpr_strdup(source);
    (*fields)[*count].expr = expr;
    if (!(*fields)[*count].name || !(*fields)[*count].source) return false;
    (*count)++;
    return true;
}

static bool cxpr_model_parse_record_return_fields(char* rest,
                                                  const cxpr_model_local_binding* locals,
                                                  size_t local_count,
                                                  cxpr_model_record_field** out_fields,
                                                  size_t* out_count,
                                                  size_t line_no,
                                                  cxpr_error* err) {
    char* close = NULL;
    char* items;
    char* item_save = NULL;
    cxpr_model_record_field* fields = NULL;
    size_t field_count = 0u;
    bool braced;

    if (!rest || !out_fields || !out_count) return false;
    *out_fields = NULL;
    *out_count = 0u;

    braced = *rest == '{';
    if (braced) {
        close = strrchr(rest, '}');
        if (!close || close[1] != '\0') {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Expected record return: return { ... }",
                                 line_no, 1);
            return false;
        }
        *close = '\0';
        items = cxpr_model_trim_in_place(rest + 1);
    } else {
        items = cxpr_model_trim_in_place(rest);
    }
    for (char* item = cxpr_strtok_r(items, ",", &item_save);
         item;
         item = cxpr_strtok_r(NULL, ",", &item_save)) {
        char* eq;
        char* field_name;
        char* expr_text;
        cxpr_ast* field_ast;
        cxpr_ast* field_expanded;
        item = cxpr_model_trim_in_place(item);
        if (*item == '\0') continue;
        eq = strchr(item, '=');
        if (eq) {
            *eq = '\0';
            field_name = cxpr_model_trim_in_place(item);
            expr_text = cxpr_model_trim_in_place(eq + 1);
        } else {
            field_name = item;
            expr_text = item;
        }
        if (!cxpr_model_is_ident(field_name)) {
            cxpr_model_record_fields_free(fields, field_count);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Invalid record return field",
                                 line_no, 1);
            return false;
        }
        field_ast = cxpr_model_parse_expr(expr_text, line_no, 1, err);
        if (!field_ast) {
            cxpr_model_record_fields_free(fields, field_count);
            return false;
        }
        field_expanded = cxpr_model_inline_locals(field_ast, locals, local_count);
        cxpr_ast_free(field_ast);
        if (!field_expanded ||
            !cxpr_model_record_fields_append(&fields, &field_count,
                                             field_name, expr_text,
                                             field_expanded)) {
            cxpr_ast_free(field_expanded);
            cxpr_model_record_fields_free(fields, field_count);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                 "Out of memory", line_no, 1);
            return false;
        }
    }
    if (field_count == 0u) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Record return requires fields", line_no, 1);
        return false;
    }

    *out_fields = fields;
    *out_count = field_count;
    return true;
}

static bool cxpr_model_parse_function_block(cxpr_model* model, char* statement,
                                            size_t line_no, cxpr_error* err) {
    char* open_brace = strchr(statement, '{');
    char* close_brace = strrchr(statement, '}');
    char* header;
    char* body;
    char* lowered;
    char* out_text = NULL;
    char* body_copy = NULL;
    char* save = NULL;
    cxpr_model_local_binding* locals = NULL;
    cxpr_model_record_field* record_fields = NULL;
    size_t record_field_count = 0u;
    size_t local_count = 0;
    size_t lowered_len;
    bool ok;

    if (!open_brace || !close_brace || close_brace < open_brace) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected function block", line_no, 1);
        return false;
    }

    *open_brace = '\0';
    *close_brace = '\0';
    header = cxpr_model_trim_in_place(statement);
    body = cxpr_model_trim_in_place(open_brace + 1);

    body_copy = cxpr_strdup(body);
    if (!body_copy) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }

    for (char* line = cxpr_strtok_r(body_copy, "\n", &save);
         line;
         line = cxpr_strtok_r(NULL, "\n", &save)) {
        const char* rest = NULL;
        char* trimmed = cxpr_model_trim_in_place(line);
        if (*trimmed == '\0') continue;

        if (cxpr_model_keyword_line(trimmed, "return", &rest) ||
            cxpr_model_keyword_line(trimmed, "out", &rest)) {
            cxpr_ast* out_ast;
            cxpr_ast* expanded;
            if (out_text || record_fields) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                free(out_text);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Function block has multiple return statements",
                                     line_no, 1);
                return false;
            }
            rest = cxpr_model_trim_in_place((char*)rest);
            if (!rest || *rest == '\0') {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected function return value",
                                     line_no, 1);
                return false;
            }
            if (*rest == '{' || strchr(rest, ',')) {
                if (!cxpr_model_parse_record_return_fields(
                        (char*)rest, locals, local_count,
                        &record_fields, &record_field_count, line_no, err)) {
                    free(body_copy);
                    cxpr_model_local_bindings_free(locals, local_count);
                    cxpr_model_record_fields_free(record_fields, record_field_count);
                    return false;
                }
                continue;
            }
            out_ast = cxpr_model_parse_expr(rest, line_no, 1, err);
            if (!out_ast) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                return false;
            }
            expanded = cxpr_model_inline_locals(out_ast, locals, local_count);
            cxpr_ast_free(out_ast);
            if (!expanded) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            out_text = cxpr_ast_to_string(expanded);
            cxpr_ast_free(expanded);
            if (!out_text) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            continue;
        }

        {
            char* eq = strchr(trimmed, '=');
            char* name;
            char* expr_text;
            cxpr_ast* ast;
            cxpr_ast* expanded;
            cxpr_model_local_binding* grown;

            if (!eq || out_text || record_fields) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                free(out_text);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     (out_text || record_fields)
                                         ? "Function local statement appears after return"
                                         : "Expected local assignment or return",
                                     line_no, 1);
                return false;
            }
            *eq = '\0';
            name = cxpr_model_trim_in_place(trimmed);
            expr_text = cxpr_model_trim_in_place(eq + 1);
            if (!cxpr_model_is_ident(name)) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid local name", line_no, 1);
                return false;
            }
            if (cxpr_model_local_lookup(locals, local_count, name)) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Duplicate local name", line_no, 1);
                return false;
            }
            ast = cxpr_model_parse_expr(expr_text, line_no, 1, err);
            if (!ast) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                return false;
            }
            expanded = cxpr_model_inline_locals(ast, locals, local_count);
            cxpr_ast_free(ast);
            if (!expanded) {
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            grown = (cxpr_model_local_binding*)realloc(
                locals, (local_count + 1u) * sizeof(cxpr_model_local_binding));
            if (!grown) {
                cxpr_ast_free(expanded);
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            locals = grown;
            locals[local_count].name = cxpr_strdup(name);
            locals[local_count].expr = expanded;
            if (!locals[local_count].name) {
                cxpr_ast_free(expanded);
                free(body_copy);
                cxpr_model_local_bindings_free(locals, local_count);
                cxpr_model_record_fields_free(record_fields, record_field_count);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            local_count++;
        }
    }

    free(body_copy);
    if (record_fields) {
        cxpr_model_record_function fn = {0};
        if (!cxpr_model_parse_function_signature(header, line_no, &fn.name,
                                                 &fn.params, &fn.param_count, err)) {
            cxpr_model_local_bindings_free(locals, local_count);
            cxpr_model_record_fields_free(record_fields, record_field_count);
            return false;
        }
        fn.fields = record_fields;
        fn.field_count = record_field_count;
        if (!cxpr_model_append_record_function(model, &fn)) {
            cxpr_model_record_function_clear(&fn);
            cxpr_model_local_bindings_free(locals, local_count);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return false;
        }
        cxpr_model_local_bindings_free(locals, local_count);
        return true;
    }
    if (!out_text) {
        cxpr_model_local_bindings_free(locals, local_count);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Function block requires a return statement",
                             line_no, 1);
        return false;
    }
    lowered_len = strlen(header) + strlen(out_text) + 4u;
    lowered = (char*)malloc(lowered_len);
    if (!lowered) {
        free(out_text);
        cxpr_model_local_bindings_free(locals, local_count);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
        return false;
    }
    snprintf(lowered, lowered_len, "%s = %s", header, out_text);
    ok = cxpr_model_parse_function(model, lowered, line_no, err);
    free(lowered);
    free(out_text);
    cxpr_model_local_bindings_free(locals, local_count);
    return ok;
}

static bool cxpr_model_parse_statement(cxpr_model* model, char* statement,
                                       size_t line_no,
                                       cxpr_error* err) {
    const char* rest = NULL;
    bool ok = false;

    statement = cxpr_model_trim_in_place(statement);
    if (*statement == '\0') return true;

    if (cxpr_model_keyword_line(statement, "name", &rest)) {
        char* owned = cxpr_strdup(rest);
        char* model_name;
        char* metadata_body = NULL;
        char* open;
        char* close;
        if (!owned) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        open = strchr(owned, '{');
        close = strrchr(owned, '}');
        if (open || close) {
            if (!open || !close || close < open || close[1] != '\0') {
                free(owned);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Expected model metadata block: name id { ... }",
                                     line_no, 1);
                goto done;
            }
            *open = '\0';
            *close = '\0';
            metadata_body = open + 1;
        }
        model_name = cxpr_model_trim_in_place(owned);
        if (!cxpr_model_is_ident(model_name)) {
            free(owned);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid model name", line_no, 1);
            goto done;
        }
        free(model->name);
        model->name = cxpr_strdup(model_name);
        if (!model->name) {
            free(owned);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        if (metadata_body) {
            cxpr_model_pending_metadata pending = {0};
            pending.name = cxpr_strdup("model");
            pending.body = cxpr_strdup(metadata_body);
            if (!pending.name || !pending.body ||
                !cxpr_model_attach_metadatas(model, &pending, 1u,
                                              CXPR_MODEL_METADATA_TARGET_MODEL,
                                              model_name)) {
                free(pending.name);
                free(pending.body);
                free(owned);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory",
                                     line_no, 1);
                goto done;
            }
            free(pending.name);
            free(pending.body);
        }
        free(owned);
        ok = true;
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "use", &rest)) {
        char* use_owned = cxpr_strdup(rest);
        const char* use_path = NULL;
        const char* use_alias = NULL;
        if (!use_owned) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        if (!cxpr_model_parse_use_clause(use_owned, &use_path, &use_alias)) {
            free(use_owned);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid use name", line_no, 1);
            goto done;
        }
        if (!cxpr_model_append_use(model, use_path, use_alias)) {
            free(use_owned);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        free(use_owned);
        ok = true;
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "in", &rest)) {
        ok = cxpr_model_parse_input_list(model, rest, line_no, err);
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "fn", &rest)) {
        char* brace = strchr(statement, '{');
        char* eq = strchr(statement, '=');
        (void)rest;
        if (brace && (!eq || brace < eq)) {
            ok = cxpr_model_parse_function_block(model, statement, line_no, err);
            goto done;
        }
        ok = cxpr_model_parse_function(model, statement, line_no, err);
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "update", &rest)) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "State updates must use ':=' assignments",
                             line_no, 1);
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "out", &rest)) {
        char* first_eq = strchr(rest, '=');
        char* first_brace = strchr(rest, '{');
        if (first_eq && (!first_brace || first_eq < first_brace)) {
            char* owned = cxpr_strdup(rest);
            if (!owned) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                goto done;
            }
            ok = cxpr_model_parse_out_assignment(model, owned, line_no, err);
            free(owned);
            goto done;
        }
        {
            const char* first = rest;
            while (*first && isspace((unsigned char)*first)) first++;
            if (strchr(rest, '{') && *first != '{') {
            char* owned = cxpr_strdup(rest);
            if (!owned) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                goto done;
            }
            ok = cxpr_model_parse_out_metadata_block(model, owned, line_no, err);
            free(owned);
            goto done;
            }
        }
        if (strchr(rest, '(')) {
            ok = cxpr_model_parse_anonymous_out(model, rest, line_no, err);
            goto done;
        }
        if (strchr(rest, '{') || strchr(rest, ',')) {
            ok = cxpr_model_parse_name_list(&model->outputs, &model->output_count,
                                            rest, "Expected output list: out { ... }",
                                            line_no, err);
            goto done;
        }
        if (!cxpr_model_is_ident(rest)) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid output name", line_no, 1);
            goto done;
        }
        if (!cxpr_model_string_set_add(&model->outputs, &model->output_count, rest)) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        ok = true;
        goto done;
    }

    if (cxpr_model_keyword_line(statement, "state", &rest)) {
        char* owned = cxpr_strdup(rest);
        if (!owned) {
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            goto done;
        }
        if (strchr(rest, '{')) {
            ok = cxpr_model_parse_assignment_block(model, owned, false, line_no, err);
        } else {
            ok = cxpr_model_parse_state_assignment(model, owned, line_no, err);
        }
        if (!ok && err && err->code == CXPR_OK) {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Invalid state assignment",
                                 line_no, 1);
        }
        free(owned);
        goto done;
    }

    if (statement[0] == '$') {
        char* after_dollar = statement + 1;
        while (*after_dollar && isspace((unsigned char)*after_dollar)) after_dollar++;
        if (*after_dollar == '{') {
        ok = cxpr_model_parse_param_block(model, statement, line_no, err);
        goto done;
        }
    }

    if (statement[0] == '$') {
        ok = cxpr_model_parse_assignment(model, statement, CXPR_MODEL_BINDING_EXPR,
                                         line_no, true, err);
        goto done;
    }

    if (strstr(statement, ":=")) {
        ok = cxpr_model_parse_state_update_assignment(model, statement, line_no, err);
        goto done;
    }

    ok = cxpr_model_parse_assignment(model, statement, CXPR_MODEL_BINDING_EXPR,
                                     line_no, false, err);

done:
    if (!ok && err && err->code == CXPR_OK) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Failed to parse model statement",
                             line_no, 1);
    }
    return ok;
}

static bool cxpr_model_statement_append(char** current, const char* text) {
    size_t old_len = *current ? strlen(*current) : 0;
    size_t add_len = strlen(text);
    char* next = (char*)realloc(*current, old_len + add_len + 2);
    if (!next) return false;
    if (old_len == 0) {
        memcpy(next, text, add_len + 1);
    } else {
        next[old_len] = '\n';
        memcpy(next + old_len + 1, text, add_len + 1);
    }
    *current = next;
    return true;
}

static int cxpr_model_brace_delta(const char* text) {
    int delta = 0;
    while (text && *text) {
        if (*text == '{') delta++;
        if (*text == '}') delta--;
        text++;
    }
    return delta;
}

static bool cxpr_model_has_top_level_comma(const char* text) {
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;

    while (text && *text) {
        switch (*text) {
        case '(':
            paren_depth++;
            break;
        case ')':
            if (paren_depth > 0) paren_depth--;
            break;
        case '{':
            brace_depth++;
            break;
        case '}':
            if (brace_depth > 0) brace_depth--;
            break;
        case '[':
            bracket_depth++;
            break;
        case ']':
            if (bracket_depth > 0) bracket_depth--;
            break;
        case ',':
            if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) return true;
            break;
        default:
            break;
        }
        text++;
    }
    return false;
}

cxpr_model* cxpr_parse_model(const char* source, cxpr_error* err) {
    cxpr_model* model;
    char* copy;
    char* line;
    char* save = NULL;
    char* current = NULL;
    char* host_kind = NULL;
    char* host_name = NULL;
    char* host_body = NULL;
    size_t current_line = 1;
    size_t line_no = 0;
    int brace_depth = 0;
    int host_brace_depth = 0;
    bool collecting_host_block = false;

    if (err) *err = (cxpr_error){0};
    if (!source) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model source", 0, 0);
        return NULL;
    }

    model = (cxpr_model*)calloc(1, sizeof(cxpr_model));
    copy = cxpr_strdup(source);
    if (!model || !copy) {
        free(model);
        free(copy);
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0, 0);
        return NULL;
    }

    for (line = cxpr_strtok_r(copy, "\n", &save); line; line = cxpr_strtok_r(NULL, "\n", &save)) {
        char* trimmed;
        char* raw_line;
        bool continuation;
        line_no++;

        if (line[0] == '\r') line++;
        raw_line = cxpr_strdup(line);
        if (!raw_line) {
            free(current);
            free(host_kind);
            free(host_name);
            free(host_body);
            free(copy);
            cxpr_model_free(model);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return NULL;
        }
        trimmed = cxpr_model_trim_in_place(line);
        if (collecting_host_block) {
            host_brace_depth += cxpr_model_brace_delta(trimmed);
            if (cxpr_model_host_block_line_uses_yaml_mapping(raw_line, NULL)) {
                free(raw_line);
                free(current);
                free(host_kind);
                free(host_name);
                free(host_body);
                free(copy);
                cxpr_model_free(model);
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Host block body must use cxpr syntax, not YAML mapping syntax",
                                     line_no, 1);
                return NULL;
            }
            if (host_brace_depth <= 0) {
                char* close = strrchr(raw_line, '}');
                if (close && close > raw_line &&
                    !cxpr_model_append_text_len(&host_body, raw_line, (size_t)(close - raw_line))) {
                    free(raw_line);
                    free(current);
                    free(host_kind);
                    free(host_name);
                    free(host_body);
                    free(copy);
                    cxpr_model_free(model);
                    if (!err || err->code == CXPR_OK) {
                        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                    }
                    return NULL;
                }
                if (!cxpr_model_append_host_block(
                        model, host_kind, host_name, host_body, line_no, err)) {
                    free(raw_line);
                    free(current);
                    free(host_kind);
                    free(host_name);
                    free(host_body);
                    free(copy);
                    cxpr_model_free(model);
                    if (!err || err->code == CXPR_OK) {
                        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                    }
                    return NULL;
                }
                free(host_kind);
                free(host_name);
                free(host_body);
                host_kind = NULL;
                host_name = NULL;
                host_body = NULL;
                host_brace_depth = 0;
                collecting_host_block = false;
            } else if (!cxpr_model_append_text(&host_body, raw_line)) {
                free(raw_line);
                free(current);
                free(host_kind);
                free(host_name);
                free(host_body);
                free(copy);
                cxpr_model_free(model);
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return NULL;
            }
            free(raw_line);
            continue;
        }
        if (*trimmed == '\0' || *trimmed == '#') {
            free(raw_line);
            continue;
        }
        if (trimmed[0] == '/' && trimmed[1] == '/') {
            free(raw_line);
            continue;
        }
        if (trimmed[0] == '@') {
            free(raw_line);
            free(current);
            free(copy);
            cxpr_model_free(model);
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Decorator metadata is not supported", line_no, 1);
            return NULL;
        }

        continuation = current && (brace_depth > 0 || (line[0] && isspace((unsigned char)line[0])));
        if (!continuation && current) {
            if (!cxpr_model_parse_statement(model, current, current_line, err)) {
                free(raw_line);
                free(current);
                free(copy);
                cxpr_model_free(model);
                return NULL;
            }
            free(current);
            current = NULL;
            brace_depth = 0;
        }

        if (!current) {
            char* block_kind = NULL;
            char* block_name = NULL;
            const char* body_start = NULL;
            if (cxpr_model_parse_host_block_start(
                    trimmed, &block_kind, &block_name, &body_start, line_no, err)) {
                host_brace_depth = cxpr_model_brace_delta(trimmed);
                if (host_brace_depth <= 0) {
                    const char* close = strrchr(body_start, '}');
                    size_t len = close && close >= body_start
                        ? (size_t)(close - body_start)
                        : strlen(body_start);
                    if (cxpr_model_host_block_line_uses_yaml_mapping(body_start, close)) {
                        free(raw_line);
                        free(block_kind);
                        free(block_name);
                        free(copy);
                        cxpr_model_free(model);
                        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                             "Host block body must use cxpr syntax, not YAML mapping syntax",
                                             line_no, 1);
                        return NULL;
                    }
                    if (!cxpr_model_append_text_len(&host_body, body_start, len) ||
                        !cxpr_model_append_host_block(
                            model, block_kind, block_name, host_body, line_no, err)) {
                        free(raw_line);
                        free(block_kind);
                        free(block_name);
                        free(host_body);
                        free(copy);
                        cxpr_model_free(model);
                        if (!err || err->code == CXPR_OK) {
                            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                        }
                        return NULL;
                    }
                    free(block_kind);
                    free(block_name);
                    free(host_body);
                    host_body = NULL;
                } else {
                    host_kind = block_kind;
                    host_name = block_name;
                    collecting_host_block = true;
                    if (cxpr_model_host_block_line_uses_yaml_mapping(body_start, NULL)) {
                        free(raw_line);
                        free(current);
                        free(host_kind);
                        free(host_name);
                        free(host_body);
                        free(copy);
                        cxpr_model_free(model);
                        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                             "Host block body must use cxpr syntax, not YAML mapping syntax",
                                             line_no, 1);
                        return NULL;
                    }
                    if (*body_start && !cxpr_model_append_text(&host_body, body_start)) {
                        free(raw_line);
                        free(current);
                        free(host_kind);
                        free(host_name);
                        free(host_body);
                        free(copy);
                        cxpr_model_free(model);
                        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                        return NULL;
                    }
                }
                free(raw_line);
                continue;
            }
            if (err && err->code != CXPR_OK) {
                free(raw_line);
                free(current);
                free(copy);
                cxpr_model_free(model);
                return NULL;
            }
        }

        if (!current) current_line = line_no;
        if (!cxpr_model_statement_append(&current, trimmed)) {
            free(raw_line);
            free(current);
            free(copy);
            cxpr_model_free(model);
            cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
            return NULL;
        }
        brace_depth += cxpr_model_brace_delta(trimmed);
        free(raw_line);
    }

    if (collecting_host_block) {
        free(current);
        free(host_kind);
        free(host_name);
        free(host_body);
        free(copy);
        cxpr_model_free(model);
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Unterminated host block", line_no, 1);
        return NULL;
    }

    if (current && !cxpr_model_parse_statement(model, current, current_line, err)) {
        free(current);
        free(host_kind);
        free(host_name);
        free(host_body);
        free(copy);
        cxpr_model_free(model);
        return NULL;
    }

    free(current);
    free(host_kind);
    free(host_name);
    free(host_body);
    free(copy);
    if (err) err->code = CXPR_OK;
    return model;
}
