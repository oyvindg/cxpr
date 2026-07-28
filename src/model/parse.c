#include "model/internal.h"

#include <cxpr/parser.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cxpr_expr_ast* cxpr_model_parse_expr(const char* expr, size_t line, size_t column,
                                       cxpr_error* err);

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

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_append_text(char** current, const char* text) {
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

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_state_exists(const cxpr_model* model, const char* name) {
    if (!model) return false;
    for (size_t i = 0; i < model->binding_count; ++i) {
        if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
            cxpr_model_names_match(model->bindings[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_keyword_line(const char* line, const char* keyword,
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
        "name", "model", "use", "in", "fn", "update", "out", "state", "meta"
    };
    if (!kind) return true;
    for (size_t i = 0u; i < CXPR_ARRAY_COUNT(reserved); ++i) {
        if (strcmp(kind, reserved[i]) == 0) return true;
    }
    return false;
}

static bool cxpr_model_host_ident_char(char ch);
static bool cxpr_model_host_name_start(char ch);

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_parse_host_block_start(const char* line,
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
        if (!cxpr_model_host_name_start(*cursor)) {
            free(*out_kind);
            *out_kind = NULL;
            return false;
        }
        cursor++;
        while (*cursor && cxpr_model_host_ident_char(*cursor)) cursor++;
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

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_parse_use_clause(char* text,
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

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_parse_use_group(cxpr_model* model, char* text) {
    char* cursor;
    char* close;
    char* after;
    char* base;
    char* item;
    char* next;
    bool saw_item = false;

    if (!model || !text) return false;
    cursor = cxpr_model_trim_in_place(text);
    if (*cursor != '{') return false;
    close = strchr(cursor, '}');
    if (!close) return false;
    *close = '\0';

    after = cxpr_model_trim_in_place(close + 1);
    if (strncmp(after, "from", 4u) != 0 ||
        (after[4] != '\0' && !isspace((unsigned char)after[4]))) {
        return false;
    }
    base = cxpr_model_trim_in_place(after + 4);
    if (!cxpr_model_is_use_path(base)) return false;

    item = cursor + 1;
    while (item) {
        size_t path_len;
        char* path;

        next = strchr(item, ',');
        if (next) *next = '\0';
        item = cxpr_model_trim_in_place(item);
        if (!cxpr_model_is_ident(item)) return false;
        path_len = strlen(base) + 1u + strlen(item);
        path = (char*)malloc(path_len + 1u);
        if (!path) return false;
        snprintf(path, path_len + 1u, "%s/%s", base, item);
        if (!cxpr_model_append_use(model, path, NULL)) {
            free(path);
            return false;
        }
        free(path);
        saw_item = true;
        item = next ? next + 1 : NULL;
    }
    return saw_item;
}

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_attach_metadatas(cxpr_model* model,
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
        out->span = (cxpr_source_span){0};
        out->has_span = false;
        if (!out->name || !out->body || !out->target_name) return false;
    }
    model->metadata_count += pending_count;
    return true;
}

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

static bool cxpr_model_host_name_start(char ch) {
    return isalnum((unsigned char)ch) || ch == '_';
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

static const char* cxpr_model_host_parse_name(const char* cursor,
                                              const char** out_start,
                                              size_t* out_len) {
    const char* start = cursor;
    if (out_start) *out_start = NULL;
    if (out_len) *out_len = 0u;
    if (!cursor || !cxpr_model_host_name_start(*cursor)) return NULL;
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
        if (*probe == '\0' || *probe == '}') {
            const char* value = "true";
            if (!cxpr_model_host_block_append_field(
                    parent,
                    key_start,
                    key_len,
                    value,
                    strlen(value))) {
                cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line_no, 1);
                return false;
            }
            *cursor = probe;
            continue;
        }
        {
            const char* name_start = NULL;
            size_t name_len = 0u;
            char* kind = NULL;
            char* name = NULL;
            cxpr_model_host_block* child = NULL;

            if (*probe != '{') {
                const char* after_name = cxpr_model_host_parse_name(probe, &name_start, &name_len);
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

static bool CXPR_MODEL_MAYBE_UNUSED
cxpr_model_append_host_block(cxpr_model* model,
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

static cxpr_expr_ast* cxpr_model_parse_expr(const char* expr, size_t line, size_t column,
                                       cxpr_error* err) {
    cxpr_parser* parser;
    cxpr_expr_ast* ast;
    if (!expr || expr[0] == '\0') {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "Expected expression", line, column);
        return NULL;
    }
    parser = cxpr_parser_new();
    if (!parser) {
        cxpr_model_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line, column);
        return NULL;
    }
    ast = cxpr_expr_ast_parse(parser, expr, err);
    cxpr_parser_free(parser);
    return ast;
}

static bool cxpr_model_collect_ast_functions(const cxpr_model* model, const cxpr_expr_ast* ast,
                                             char*** names, size_t* count) {
    const char* used[256];
    size_t used_count = cxpr_expr_ast_functions_used(ast, used, CXPR_ARRAY_COUNT(used));
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
    cxpr_expr_ast* body;
    bool ok;
    if (!arrow) return true;
    arrow += 2;
    while (*arrow && isspace((unsigned char)*arrow)) arrow++;
    body = cxpr_model_parse_expr(arrow, 0, 0, err);
    if (!body) return false;
    ok = cxpr_model_collect_ast_functions(model, body, names, count);
    cxpr_expr_ast_free(body);
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
