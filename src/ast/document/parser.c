/**
 * @file ast/document/parser.c
 * @brief Block-aware document AST parser and lifecycle.
 */

#include <cxpr/doc/ast.h>

#include "ast/document/internal.h"
#include "core.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* source;
    unsigned extensions;
    size_t length;
    cxpr_error* err;
} cxpr_doc_ast_parser;

static bool cxpr_doc_ast_parse_host_statement(cxpr_doc_ast_parser* parser,
                                                   cxpr_doc_ast_node* root,
                                                   const char* text,
                                                   size_t start_offset,
                                                   size_t end_offset);

static void cxpr_doc_ast_set_error(cxpr_error* err,
                                        cxpr_error_code code,
                                        const char* message,
                                        size_t line,
                                        size_t column) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->line = line;
    err->column = column;
}

static char* cxpr_doc_ast_substr(const char* start, size_t len) {
    char* out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static char* cxpr_doc_ast_trim_copy(const char* start, size_t len) {
    while (len > 0u && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len > 0u && isspace((unsigned char)start[len - 1u])) len--;
    if (len > 0u && start[len - 1u] == ';') {
        len--;
        while (len > 0u && isspace((unsigned char)start[len - 1u])) len--;
    }
    return cxpr_doc_ast_substr(start, len);
}

static char* cxpr_doc_ast_strip_comments(const char* source) {
    char* out;
    size_t len;
    bool in_line_comment = false;
    bool in_block_comment = false;
    char quote = '\0';

    if (!source) return NULL;
    len = strlen(source);
    out = cxpr_doc_ast_substr(source, len);
    if (!out) return NULL;

    for (size_t i = 0u; i < len; ++i) {
        char ch = out[i];
        if (in_line_comment) {
            if (ch == '\n') {
                in_line_comment = false;
            } else {
                out[i] = ' ';
            }
            continue;
        }
        if (in_block_comment) {
            if (ch == '*' && i + 1u < len && out[i + 1u] == '/') {
                out[i] = ' ';
                out[i + 1u] = ' ';
                i++;
                in_block_comment = false;
            } else if (ch != '\n') {
                out[i] = ' ';
            }
            continue;
        }
        if (quote) {
            if (ch == '\\' && i + 1u < len) {
                i++;
                continue;
            }
            if (ch == quote) quote = '\0';
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            continue;
        }
        if (ch == '#') {
            out[i] = ' ';
            in_line_comment = true;
            continue;
        }
        if (ch == '/' && i + 1u < len && out[i + 1u] == '/') {
            out[i] = ' ';
            out[i + 1u] = ' ';
            i++;
            in_line_comment = true;
            continue;
        }
        if (ch == '/' && i + 1u < len && out[i + 1u] == '*') {
            out[i] = ' ';
            out[i + 1u] = ' ';
            i++;
            in_block_comment = true;
        }
    }
    return out;
}

static bool cxpr_doc_ast_is_ident(const char* s) {
    if (!s || !(isalpha((unsigned char)*s) || *s == '_')) return false;
    for (s++; *s; ++s) {
        if (!(isalnum((unsigned char)*s) || *s == '_')) return false;
    }
    return true;
}

static char* cxpr_doc_ast_trim_in_place(char* s) {
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

static bool cxpr_doc_ast_keyword(const char* line,
                                      const char* keyword,
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

static bool cxpr_doc_ast_append_continuation(char** text, const char* part) {
    char* grown;
    size_t old_len;
    size_t part_len;
    size_t sep_len;

    if (!text || !part) return false;
    old_len = *text ? strlen(*text) : 0u;
    part_len = strlen(part);
    sep_len = old_len > 0u && part_len > 0u ? 1u : 0u;
    grown = (char*)realloc(*text, old_len + sep_len + part_len + 1u);
    if (!grown) return false;
    if (sep_len > 0u) grown[old_len++] = ' ';
    memcpy(grown + old_len, part, part_len);
    grown[old_len + part_len] = '\0';
    *text = grown;
    return true;
}

static cxpr_source_pos cxpr_doc_ast_pos(const cxpr_doc_ast_parser* parser,
                                             size_t offset) {
    cxpr_source_pos pos = {0u, 1u, 0u};
    size_t i;
    if (!parser || !parser->source) return pos;
    if (offset > parser->length) offset = parser->length;
    for (i = 0u; i < offset; ++i) {
        if (parser->source[i] == '\n') {
            pos.line++;
            pos.column = 0u;
        } else {
            pos.column++;
        }
    }
    pos.offset = offset;
    return pos;
}

static cxpr_source_span cxpr_doc_ast_span(const cxpr_doc_ast_parser* parser,
                                               size_t start,
                                               size_t end) {
    cxpr_source_span span;
    span.start = cxpr_doc_ast_pos(parser, start);
    span.end = cxpr_doc_ast_pos(parser, end);
    return span;
}

static cxpr_doc_ast_node* cxpr_doc_ast_node_new(
    cxpr_doc_ast_kind kind,
    cxpr_source_span span) {
    cxpr_doc_ast_node* node = (cxpr_doc_ast_node*)calloc(1u, sizeof(*node));
    if (!node) return NULL;
    node->kind = kind;
    node->span = span;
    return node;
}

static bool cxpr_doc_ast_append_child(cxpr_doc_ast_node* parent,
                                           cxpr_doc_ast_node* child) {
    cxpr_doc_ast_node** grown;
    size_t next_capacity;
    if (!parent || !child) return false;
    if (parent->children.count == parent->children.capacity) {
        next_capacity = parent->children.capacity ? parent->children.capacity * 2u : 4u;
        grown = (cxpr_doc_ast_node**)realloc(
            parent->children.items, next_capacity * sizeof(*parent->children.items));
        if (!grown) return false;
        parent->children.items = grown;
        parent->children.capacity = next_capacity;
    }
    parent->children.items[parent->children.count++] = child;
    return true;
}

static void cxpr_doc_ast_node_free(cxpr_doc_ast_node* node) {
    if (!node) return;
    for (size_t i = 0u; i < node->children.count; ++i) {
        cxpr_doc_ast_node_free(node->children.items[i]);
    }
    free(node->children.items);
    free(node->name);
    free(node->value);
    free(node->text);
    cxpr_expr_ast_free(node->expression);
    free(node);
}

static int cxpr_doc_ast_brace_delta(const char* text) {
    int delta = 0;
    char quote = '\0';
    while (text && *text) {
        if (quote) {
            if (*text == '\\' && text[1]) {
                text += 2;
                continue;
            }
            if (*text == quote) quote = '\0';
        } else if (*text == '"' || *text == '\'') {
            quote = *text;
        } else if (*text == '{') {
            delta++;
        } else if (*text == '}') {
            delta--;
        }
        text++;
    }
    return delta;
}

static bool cxpr_doc_ast_has_top_level_comma(const char* text) {
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

static bool cxpr_doc_ast_reserved_host_kind(const char* kind) {
    static const char* reserved[] = {
        "name", "model", "use", "in", "fn", "update", "out", "state", "meta"
    };
    for (size_t i = 0u; i < CXPR_ARRAY_COUNT(reserved); ++i) {
        if (strcmp(kind, reserved[i]) == 0) return true;
    }
    return false;
}

static bool cxpr_doc_ast_host_name_start(char ch) {
    return isalnum((unsigned char)ch) || ch == '_';
}

static bool cxpr_doc_ast_host_name_char(char ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '-';
}

static bool cxpr_doc_ast_parse_host_start(const char* line,
                                               char** out_kind,
                                               char** out_name,
                                               const char** out_body) {
    const char* cursor = line;
    const char* kind_start;
    const char* open;
    const char* name_start = NULL;
    const char* name_end = NULL;
    *out_kind = NULL;
    *out_name = NULL;
    *out_body = NULL;
    if (!line || !(isalpha((unsigned char)*cursor) || *cursor == '_')) return false;
    open = strchr(line, '{');
    if (!open) return false;
    if (strchr(line, '=') && strchr(line, '=') < open) return false;
    kind_start = cursor++;
    while (isalnum((unsigned char)*cursor) || *cursor == '_') cursor++;
    *out_kind = cxpr_doc_ast_substr(kind_start, (size_t)(cursor - kind_start));
    if (!*out_kind) return false;
    if (cxpr_doc_ast_reserved_host_kind(*out_kind)) {
        free(*out_kind);
        *out_kind = NULL;
        return false;
    }
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (cursor < open) {
        name_start = cursor;
        if (!cxpr_doc_ast_host_name_start(*cursor)) {
            free(*out_kind);
            *out_kind = NULL;
            return false;
        }
        cursor++;
        while (cxpr_doc_ast_host_name_char(*cursor)) cursor++;
        name_end = cursor;
        while (*cursor && isspace((unsigned char)*cursor)) cursor++;
        if (cursor != open) {
            free(*out_kind);
            *out_kind = NULL;
            return false;
        }
    }
    *out_name = name_start ? cxpr_doc_ast_substr(name_start, (size_t)(name_end - name_start))
                           : cxpr_strdup("");
    if (!*out_name) {
        free(*out_kind);
        *out_kind = NULL;
        return false;
    }
    *out_body = open + 1;
    return true;
}

static cxpr_expr_ast* cxpr_doc_ast_parse_expr(const char* text,
                                              size_t line,
                                              size_t column,
                                              cxpr_error* err) {
    cxpr_expr_parser* parser;
    cxpr_expr_ast* ast;
    cxpr_error inner = {0};
    if (!text || *text == '\0') {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX, "Expected expression", line, column);
        return NULL;
    }
    parser = cxpr_expr_parser_new();
    if (!parser) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", line, column);
        return NULL;
    }
    ast = cxpr_expr_ast_parse(parser, text, &inner);
    cxpr_expr_parser_free(parser);
    if (!ast && err) {
        *err = inner;
        err->line = line + (inner.line > 0u ? inner.line - 1u : 0u);
        err->column = column + (inner.column > 0u ? inner.column - 1u : 0u);
    }
    return ast;
}

static bool cxpr_doc_ast_assign_name_expr(cxpr_doc_ast_node* node,
                                               cxpr_doc_ast_parser* parser,
                                               char* statement,
                                               bool strip_param,
                                               size_t statement_offset,
                                               size_t line,
                                               size_t column,
                                               cxpr_error* err) {
    char* eq = strchr(statement, '=');
    char* name;
    char* expr;
    char* metadata_open;
    char* metadata_close = NULL;
    cxpr_doc_ast_node* metadata = NULL;
    if (!eq) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX, "Expected '='", line, column);
        return false;
    }
    *eq = '\0';
    name = cxpr_doc_ast_trim_in_place(statement);
    expr = cxpr_doc_ast_trim_in_place(eq + 1);
    metadata_open = *expr == '{' ? NULL : strchr(expr, '{');
    if (metadata_open) {
        metadata_close = strrchr(metadata_open, '}');
        *metadata_open = '\0';
        expr = cxpr_doc_ast_trim_in_place(expr);
    }
    if (strip_param && name[0] == '$') name++;
    if (!cxpr_doc_ast_is_ident(name)) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX, "Invalid symbol name", line, column);
        return false;
    }
    node->name = cxpr_strdup(name);
    node->text = cxpr_strdup(expr);
    node->expression = cxpr_doc_ast_parse_expr(expr, line, column, err);
    if (!node->name || !node->text || !node->expression) return false;
    if (!metadata_open) return true;
    if (!metadata_close || metadata_close < metadata_open) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX,
                                    "Expected metadata block after assignment", line, column);
        return false;
    }
    *metadata_close = '\0';
    metadata = cxpr_doc_ast_node_new(
        CXPR_DOC_AST_METADATA,
        cxpr_doc_ast_span(parser,
                               statement_offset + (size_t)(metadata_open - statement),
                               statement_offset + (size_t)(metadata_close - statement) + 1u));
    if (!metadata) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", line, column);
        return false;
    }
    metadata->name = cxpr_strdup("metadata");
    metadata->text = cxpr_strdup(cxpr_doc_ast_trim_in_place(metadata_open + 1));
    if (!metadata->name || !metadata->text ||
        !cxpr_doc_ast_append_child(node, metadata)) {
        cxpr_doc_ast_node_free(metadata);
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", line, column);
        return false;
    }
    return true;
}

static bool cxpr_doc_ast_assign_name_update(cxpr_doc_ast_node* node,
                                                 char* statement,
                                                 size_t line,
                                                 size_t column,
                                                 cxpr_error* err) {
    char* op = strstr(statement, ":=");
    char* name;
    char* expr;
    if (!op) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX, "Expected ':='", line, column);
        return false;
    }
    *op = '\0';
    name = cxpr_doc_ast_trim_in_place(statement);
    expr = cxpr_doc_ast_trim_in_place(op + 2);
    if (!cxpr_doc_ast_is_ident(name)) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX,
                                    "Invalid state update name", line, column);
        return false;
    }
    node->name = cxpr_strdup(name);
    node->text = cxpr_strdup(expr);
    node->expression = cxpr_doc_ast_parse_expr(expr, line, column, err);
    return node->name && node->text && node->expression;
}

static char* cxpr_doc_ast_find_top_level_keyword(char* text, const char* keyword) {
    int paren = 0;
    int brace = 0;
    int bracket = 0;
    char quote = '\0';
    size_t keyword_len = strlen(keyword);
    char* cursor = text;

    while (cursor && *cursor) {
        char ch = *cursor;
        if (quote) {
            if (ch == '\\' && cursor[1]) {
                cursor += 2;
                continue;
            }
            if (ch == quote) quote = '\0';
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') {
            paren++;
        } else if (ch == ')' && paren > 0) {
            paren--;
        } else if (ch == '{') {
            brace++;
        } else if (ch == '}' && brace > 0) {
            brace--;
        } else if (ch == '[') {
            bracket++;
        } else if (ch == ']' && bracket > 0) {
            bracket--;
        } else if (paren == 0 && brace == 0 && bracket == 0 &&
                   strncmp(cursor, keyword, keyword_len) == 0 &&
                   (cursor == text || isspace((unsigned char)cursor[-1])) &&
                   isspace((unsigned char)cursor[keyword_len])) {
            return cursor;
        }
        cursor++;
    }
    return NULL;
}

static bool cxpr_doc_ast_assign_initial_state_update(
    cxpr_doc_ast_node* node,
    char* statement,
    size_t line,
    size_t column,
    cxpr_error* err) {
    char* op = strstr(statement, ":=");
    char* initial;
    char* name;
    char* update_expr;
    char* initial_expr;
    cxpr_doc_ast_node* declaration;

    if (!op) return false;
    initial = cxpr_doc_ast_find_top_level_keyword(op + 2, "initial");
    if (!initial) return false;
    *initial = '\0';
    initial_expr = cxpr_doc_ast_trim_in_place(initial + strlen("initial"));
    *op = '\0';
    name = cxpr_doc_ast_trim_in_place(statement);
    update_expr = cxpr_doc_ast_trim_in_place(op + 2);
    if (!cxpr_doc_ast_is_ident(name)) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX,
                                    "Invalid state update name", line, column);
        return false;
    }
    if (*update_expr == '\0' || *initial_expr == '\0') {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX,
                                    "Expected expressions before and after 'initial'",
                                    line, column);
        return false;
    }

    node->name = cxpr_strdup(name);
    node->text = cxpr_strdup(update_expr);
    node->expression = cxpr_doc_ast_parse_expr(update_expr, line, column, err);
    declaration = cxpr_doc_ast_node_new(
        CXPR_DOC_AST_STATE_DECL,
        node->span);
    if (!node->name || !node->text || !node->expression || !declaration) {
        cxpr_doc_ast_node_free(declaration);
        return false;
    }
    declaration->name = cxpr_strdup(name);
    declaration->text = cxpr_strdup(initial_expr);
    declaration->expression = cxpr_doc_ast_parse_expr(initial_expr, line, column, err);
    if (!declaration->name || !declaration->text || !declaration->expression ||
        !cxpr_doc_ast_append_child(node, declaration)) {
        cxpr_doc_ast_node_free(declaration);
        return false;
    }
    return true;
}

static bool cxpr_doc_ast_parse_comma_or_line_decls(
    cxpr_doc_ast_parser* parser,
    cxpr_doc_ast_node* block,
    cxpr_doc_ast_kind child_kind,
    const char* body,
    size_t body_offset,
    bool params) {
    const char* start = body;
    const char* cursor = body;
    int paren = 0;
    int brace = 0;
    int bracket = 0;
    for (;;) {
        char ch = *cursor;
        bool end = ch == '\0';
        bool split = false;
        if (!end) {
            if (ch == '(') paren++;
            else if (ch == ')' && paren > 0) paren--;
            else if (ch == '{') brace++;
            else if (ch == '}' && brace > 0) brace--;
            else if (ch == '[') bracket++;
            else if (ch == ']' && bracket > 0) bracket--;
            else if ((ch == ',' || ch == '\n') && paren == 0 && brace == 0 && bracket == 0) {
                split = true;
            }
        }
        if (end || split) {
            size_t len = (size_t)(cursor - start);
            char* entry = cxpr_doc_ast_trim_copy(start, len);
            if (!entry) {
                cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                            "Out of memory", 0u, 0u);
                return false;
            }
            if (*entry) {
                size_t rel = (size_t)(start - body);
                cxpr_doc_ast_node* child = cxpr_doc_ast_node_new(
                    child_kind,
                    cxpr_doc_ast_span(parser, body_offset + rel, body_offset + rel + len));
                if (!child) {
                    free(entry);
                    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                                "Out of memory", 0u, 0u);
                    return false;
                }
                if (child_kind == CXPR_DOC_AST_INPUT_DECL && strchr(entry, '=') &&
                    entry[0] != '$') {
                    const size_t error_line = child->span.start.line;
                    const size_t error_column = child->span.start.column + 1u;
                    cxpr_doc_ast_node_free(child);
                    free(entry);
                    cxpr_doc_ast_set_error(
                        parser->err, CXPR_ERR_SYNTAX,
                        "Defaulted input parameters must use '$name = default'",
                        error_line, error_column);
                    return false;
                }
                if (params || child_kind == CXPR_DOC_AST_STATE_DECL ||
                    (child_kind == CXPR_DOC_AST_INPUT_DECL && strchr(entry, '='))) {
                    if (!cxpr_doc_ast_assign_name_expr(
                            child, parser, entry,
                            params || child_kind == CXPR_DOC_AST_INPUT_DECL,
                            body_offset + rel,
                            child->span.start.line,
                            child->span.start.column + 1u, parser->err)) {
                        cxpr_doc_ast_node_free(child);
                        free(entry);
                        return false;
                    }
                } else {
                    child->name = cxpr_strdup(entry);
                    if (!child->name) {
                        cxpr_doc_ast_node_free(child);
                        free(entry);
                        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                                    "Out of memory", 0u, 0u);
                        return false;
                    }
                }
                if (!cxpr_doc_ast_append_child(block, child)) {
                    cxpr_doc_ast_node_free(child);
                    free(entry);
                    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                                "Out of memory", 0u, 0u);
                    return false;
                }
            }
            free(entry);
            if (end) break;
            start = cursor + 1;
        }
        cursor++;
    }
    return true;
}

static bool cxpr_doc_ast_parse_struct_input_decls(
    cxpr_doc_ast_parser* parser,
    cxpr_doc_ast_node* block,
    const char* root,
    const char* body,
    size_t body_offset) {
    if (!root || !*root) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_SYNTAX,
                                    "Expected input struct name", 0u, 0u);
        return false;
    }
    block->name = cxpr_strdup(root);
    if (!block->name) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", 0u, 0u);
        return false;
    }
    return cxpr_doc_ast_parse_comma_or_line_decls(
        parser, block, CXPR_DOC_AST_INPUT_DECL, body, body_offset, false);
}

static bool cxpr_doc_ast_parse_function_body(cxpr_doc_ast_parser* parser,
                                                  cxpr_doc_ast_node* function,
                                                  char* body,
                                                  size_t body_offset) {
    cxpr_doc_ast_node* body_node;
    char* cursor;
    char* save = NULL;
    size_t rel_offset = 0u;

    body_node = cxpr_doc_ast_node_new(
        CXPR_DOC_AST_FUNCTION_BODY,
        cxpr_doc_ast_span(parser, body_offset, body_offset + strlen(body)));
    if (!body_node) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", 0u, 0u);
        return false;
    }
    for (cursor = cxpr_strtok_r(body, "\n", &save); cursor;
         cursor = cxpr_strtok_r(NULL, "\n", &save)) {
        char* trimmed = cxpr_doc_ast_trim_in_place(cursor);
        size_t line_offset = body_offset + rel_offset;
        const char* rest = NULL;
        cxpr_doc_ast_node* child = NULL;
        char* eq;

        rel_offset += strlen(cursor) + 1u;
        if (*trimmed == '\0' || *trimmed == '#') continue;
        if (trimmed[0] == '/' && trimmed[1] == '/') continue;

        if (cxpr_doc_ast_keyword(trimmed, "return", &rest) ||
            cxpr_doc_ast_keyword(trimmed, "out", &rest)) {
            char* expr_text;
            char* continuation = NULL;
            char* parse_text = NULL;
            bool braced_record = false;
            child = cxpr_doc_ast_node_new(
                CXPR_DOC_AST_RETURN,
                cxpr_doc_ast_span(parser, line_offset, line_offset + strlen(trimmed)));
            if (!child) goto oom;
            if (*rest == '\0') {
                char* next;
                while ((next = cxpr_strtok_r(NULL, "\n", &save)) != NULL) {
                    char* next_trimmed = cxpr_doc_ast_trim_in_place(next);
                    rel_offset += strlen(next) + 1u;
                    if (*next_trimmed == '\0' || *next_trimmed == '#') continue;
                    if (next_trimmed[0] == '/' && next_trimmed[1] == '/') continue;
                    if (!cxpr_doc_ast_append_continuation(&continuation, next_trimmed)) {
                        free(continuation);
                        goto oom_child;
                    }
                }
            }
            child->text = cxpr_strdup(continuation ? continuation : rest);
            free(continuation);
            if (!child->text) goto oom_child;
            parse_text = cxpr_strdup(child->text);
            if (!parse_text) goto oom_child;
            expr_text = cxpr_doc_ast_trim_in_place(parse_text);
            if (*expr_text == '{') {
                char* close = strrchr(expr_text, '}');
                if (!close || close < expr_text) {
                    free(parse_text);
                    goto fail_child;
                }
                *close = '\0';
                expr_text = cxpr_doc_ast_trim_in_place(expr_text + 1);
                braced_record = true;
            }
            if (!braced_record && !cxpr_doc_ast_has_top_level_comma(expr_text)) {
                child->expression = cxpr_doc_ast_parse_expr(
                    expr_text, child->span.start.line, child->span.start.column + 1u, parser->err);
                if (!child->expression) {
                    free(parse_text);
                    goto fail_child;
                }
            }
            free(parse_text);
        } else if ((eq = strchr(trimmed, '=')) != NULL) {
            child = cxpr_doc_ast_node_new(
                CXPR_DOC_AST_LOCAL_BINDING,
                cxpr_doc_ast_span(parser, line_offset, line_offset + strlen(trimmed)));
            if (!child) goto oom;
            if (!cxpr_doc_ast_assign_name_expr(
                    child, parser, trimmed, false, line_offset, child->span.start.line,
                    child->span.start.column + 1u, parser->err)) {
                goto fail_child;
            }
            (void)eq;
        } else {
            cxpr_doc_ast_set_error(parser->err, CXPR_ERR_SYNTAX,
                                        "Expected local assignment or return",
                                        cxpr_doc_ast_pos(parser, line_offset).line,
                                        cxpr_doc_ast_pos(parser, line_offset).column + 1u);
            goto fail;
        }

        if (!cxpr_doc_ast_append_child(body_node, child)) goto oom_child;
        continue;

oom_child:
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", 0u, 0u);
fail_child:
        cxpr_doc_ast_node_free(child);
        goto fail;
oom:
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", 0u, 0u);
        goto fail;
    }
    if (!cxpr_doc_ast_append_child(function, body_node)) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                    "Out of memory", 0u, 0u);
        goto fail;
    }
    return true;

fail:
    cxpr_doc_ast_node_free(body_node);
    return false;
}

static bool cxpr_doc_ast_host_value_contains_assignment(const char* value) {
    const char* cursor = value;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    char quote = '\0';
    while (cursor && *cursor) {
        char ch = *cursor;
        if (quote) {
            if (ch == '\\' && cursor[1]) {
                cursor += 2;
                continue;
            }
            if (ch == quote) quote = '\0';
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '(') paren++;
        else if (ch == ')' && paren > 0) paren--;
        else if (ch == '[') bracket++;
        else if (ch == ']' && bracket > 0) bracket--;
        else if (ch == '{') brace++;
        else if (ch == '}' && brace > 0) brace--;
        else if (ch == '=' && paren == 0 && bracket == 0 && brace == 0) {
            return true;
        }
        cursor++;
    }
    return false;
}

static bool cxpr_doc_ast_parse_host_fields(cxpr_doc_ast_parser* parser,
                                                cxpr_doc_ast_node* host,
                                                const char* body,
                                                size_t body_offset) {
    const char* start = body;
    const char* cursor = body;
    int paren = 0;
    int bracket = 0;
    int brace = 0;
    char quote = '\0';

    for (;;) {
        char ch = *cursor;
        bool end = ch == '\0';
        bool split = false;
        if (!end) {
            if (quote) {
                if (ch == '\\' && cursor[1]) {
                    cursor += 2;
                    continue;
                }
                if (ch == quote) quote = '\0';
            } else if (ch == '"' || ch == '\'') {
                quote = ch;
            } else if (ch == '(') paren++;
            else if (ch == ')' && paren > 0) paren--;
            else if (ch == '[') bracket++;
            else if (ch == ']' && bracket > 0) bracket--;
            else if (ch == '{') brace++;
            else if (ch == '}' && brace > 0) brace--;
            else if ((ch == ',' || ch == '\n') && paren == 0 && bracket == 0 && brace == 0) {
                split = true;
            }
        }
        if (end || split) {
            size_t len = (size_t)(cursor - start);
            char* field = cxpr_doc_ast_trim_copy(start, len);
            if (!field) {
                cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                            "Out of memory", 0u, 0u);
                return false;
            }
            if (*field && strchr(field, '{')) {
                size_t rel = (size_t)(start - body);
                bool ok = cxpr_doc_ast_parse_host_statement(
                    parser,
                    host,
                    start,
                    body_offset + rel,
                    body_offset + rel + len);
                if (!ok) return false;
            } else if (*field) {
                size_t rel = (size_t)(start - body);
                char* eq = strchr(field, '=');
                cxpr_doc_ast_node* child;
                if (!eq && strchr(field, ':')) {
                    free(field);
                    cxpr_doc_ast_set_error(
                        parser->err,
                        CXPR_ERR_SYNTAX,
                        "Host block body must use cxpr syntax",
                        0u,
                        0u);
                    return false;
                }
                child = cxpr_doc_ast_node_new(
                    CXPR_DOC_AST_HOST_FIELD,
                    cxpr_doc_ast_span(parser, body_offset + rel, body_offset + rel + len));
                if (!child) {
                    free(field);
                    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                                "Out of memory", 0u, 0u);
                    return false;
                }
                if (eq) {
                    *eq = '\0';
                    if (cxpr_doc_ast_host_value_contains_assignment(eq + 1)) {
                        cxpr_doc_ast_node_free(child);
                        free(field);
                        cxpr_doc_ast_set_error(
                            parser->err,
                            CXPR_ERR_SYNTAX,
                            "Host block field value contains another assignment; use comma or newline",
                            0u,
                            0u);
                        return false;
                    }
                    child->name = cxpr_strdup(cxpr_doc_ast_trim_in_place(field));
                    child->text = cxpr_strdup(cxpr_doc_ast_trim_in_place(eq + 1));
                } else {
                    child->name = cxpr_strdup(cxpr_doc_ast_trim_in_place(field));
                    child->text = cxpr_strdup("true");
                }
                if (!child->name || !child->text ||
                    !cxpr_doc_ast_append_child(host, child)) {
                    cxpr_doc_ast_node_free(child);
                    free(field);
                    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                                "Out of memory", 0u, 0u);
                    return false;
                }
            }
            free(field);
            if (end) break;
            start = cursor + 1;
        }
        cursor++;
    }
    return true;
}

static bool cxpr_doc_ast_parse_statement(cxpr_doc_ast_parser* parser,
                                              cxpr_doc_ast_node* root,
                                              const char* text,
                                              size_t start_offset,
                                              size_t end_offset) {
    char* owned = cxpr_doc_ast_trim_copy(text, end_offset - start_offset);
    char* statement;
    const char* rest = NULL;
    cxpr_doc_ast_node* node = NULL;
    cxpr_source_span span = cxpr_doc_ast_span(parser, start_offset, end_offset);
    bool ok = false;
    if (!owned) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return false;
    }
    statement = cxpr_doc_ast_trim_in_place(owned);
    if (*statement == '\0' || *statement == '#') {
        free(owned);
        return true;
    }
    if (statement[0] == '/' && statement[1] == '/') {
        free(owned);
        return true;
    }

    if (cxpr_doc_ast_keyword(statement, "model", &rest)) {
        node = cxpr_doc_ast_node_new(CXPR_DOC_AST_MODEL_DECL, span);
        if (!node) goto oom;
        {
            char* body = strchr((char*)rest, '{');
            char* close = body ? strrchr(body, '}') : NULL;
            if (body) *body = '\0';
            node->name = cxpr_strdup(cxpr_doc_ast_trim_in_place((char*)rest));
            if (body) {
                cxpr_doc_ast_node* metadata;
                if (!close || close < body) goto syntax;
                *close = '\0';
                metadata = cxpr_doc_ast_node_new(
                    CXPR_DOC_AST_METADATA,
                    cxpr_doc_ast_span(parser,
                                           start_offset + (size_t)(body - statement),
                                           start_offset + (size_t)(close - statement) + 1u));
                if (!metadata) goto oom;
                metadata->name = cxpr_strdup("metadata");
                metadata->text = cxpr_strdup(cxpr_doc_ast_trim_in_place(body + 1));
                ok = node->name && metadata->name && metadata->text &&
                     cxpr_doc_ast_parse_host_fields(
                         parser,
                         metadata,
                         metadata->text,
                         start_offset + (size_t)(body - statement) + 1u) &&
                     cxpr_doc_ast_append_child(node, metadata);
                if (!ok) cxpr_doc_ast_node_free(metadata);
            } else {
                ok = node->name != NULL;
            }
        }
    } else if (cxpr_doc_ast_keyword(statement, "use", &rest)) {
        node = cxpr_doc_ast_node_new(CXPR_DOC_AST_USE, span);
        if (!node) goto oom;
        node->text = cxpr_strdup(rest);
        ok = node->text != NULL;
    } else if (cxpr_doc_ast_keyword(statement, "in", &rest)) {
        node = cxpr_doc_ast_node_new(strchr(rest, '{') ? CXPR_DOC_AST_INPUT_BLOCK
                                                           : CXPR_DOC_AST_INPUT_DECL,
                                          span);
        if (!node) goto oom;
        if (node->kind == CXPR_DOC_AST_INPUT_BLOCK) {
            char* open = strchr((char*)rest, '{');
            char* close = strrchr((char*)rest, '}');
            char* root;
            if (!open || !close || close < open) goto syntax;
            *close = '\0';
            *open = '\0';
            root = cxpr_doc_ast_trim_in_place((char*)rest);
            if (*root) {
                ok = cxpr_doc_ast_parse_struct_input_decls(
                    parser, node, root, open + 1,
                    start_offset + (size_t)(open + 1 - statement));
            } else {
                ok = cxpr_doc_ast_parse_comma_or_line_decls(
                    parser, node, CXPR_DOC_AST_INPUT_DECL, open + 1,
                    start_offset + (size_t)(open + 1 - statement), false);
            }
        } else if (strchr(rest, '=') || strchr(rest, ',')) {
            node->kind = CXPR_DOC_AST_INPUT_BLOCK;
            ok = cxpr_doc_ast_parse_comma_or_line_decls(
                parser, node, CXPR_DOC_AST_INPUT_DECL, rest,
                start_offset + (size_t)(rest - statement), false);
        } else {
            node->name = cxpr_strdup(rest);
            ok = node->name != NULL;
        }
    } else if (cxpr_doc_ast_keyword(statement, "state", &rest)) {
        if (strchr(rest, '{')) {
            char* open = strchr((char*)rest, '{');
            char* close = strrchr((char*)rest, '}');
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_STATE_BLOCK, span);
            if (!node) goto oom;
            if (!open || !close || close < open) goto syntax;
            *close = '\0';
            ok = cxpr_doc_ast_parse_comma_or_line_decls(
                parser, node, CXPR_DOC_AST_STATE_DECL, open + 1,
                start_offset + (size_t)(open + 1 - statement), false);
        } else {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_STATE_DECL, span);
            if (!node) goto oom;
            ok = cxpr_doc_ast_assign_name_expr(
                node, parser, (char*)rest, false, start_offset + (size_t)(rest - statement),
                span.start.line, span.start.column + 1u, parser->err);
        }
    } else if (cxpr_doc_ast_keyword(statement, "out", &rest)) {
        if (strstr(rest, ":=")) {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_OUTPUT_STATE_UPDATE, span);
            if (!node) goto oom;
            ok = cxpr_doc_ast_assign_name_update(
                node, (char*)rest, span.start.line, span.start.column + 1u, parser->err);
        } else if (strchr(rest, '(') && !strchr(rest, '=')) {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_ANONYMOUS_OUTPUT, span);
            if (!node) goto oom;
            node->text = cxpr_strdup(rest);
            node->expression = cxpr_doc_ast_parse_expr(
                rest, span.start.line, span.start.column + 1u, parser->err);
            ok = node->text && node->expression;
        } else if (strchr(rest, '{') && *rest != '{') {
            char* open = strchr((char*)rest, '{');
            char* close = strrchr((char*)rest, '}');
            cxpr_doc_ast_node* metadata;
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_OUTPUT_DECL, span);
            if (!node) goto oom;
            if (!close || close < open) goto syntax;
            *open = '\0';
            *close = '\0';
            node->name = cxpr_strdup(cxpr_doc_ast_trim_in_place((char*)rest));
            metadata = cxpr_doc_ast_node_new(
                CXPR_DOC_AST_METADATA,
                cxpr_doc_ast_span(parser,
                                       start_offset + (size_t)(open - statement),
                                       start_offset + (size_t)(close - statement) + 1u));
            if (!metadata) goto oom;
            metadata->name = cxpr_strdup("metadata");
            metadata->text = cxpr_strdup(cxpr_doc_ast_trim_in_place(open + 1));
            ok = node->name != NULL && metadata->name && metadata->text &&
                 cxpr_doc_ast_append_child(node, metadata);
            if (!ok) cxpr_doc_ast_node_free(metadata);
        } else if (strchr(rest, '{') && !strchr(rest, '=')) {
            char* open = strchr((char*)rest, '{');
            char* close = strrchr((char*)rest, '}');
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_OUTPUT_BLOCK, span);
            if (!node) goto oom;
            if (!open || !close || close < open) goto syntax;
            *close = '\0';
            ok = cxpr_doc_ast_parse_comma_or_line_decls(
                parser, node, CXPR_DOC_AST_OUTPUT_DECL, open + 1,
                start_offset + (size_t)(open + 1 - statement), false);
        } else if (strchr(rest, '=')) {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_OUTPUT_DECL, span);
            if (!node) goto oom;
            ok = cxpr_doc_ast_assign_name_expr(
                node, parser, (char*)rest, false, start_offset + (size_t)(rest - statement),
                span.start.line, span.start.column + 1u, parser->err);
        } else {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_OUTPUT_DECL, span);
            if (!node) goto oom;
            node->name = cxpr_strdup(rest);
            ok = node->name != NULL;
        }
    } else if (cxpr_doc_ast_keyword(statement, "fn", &rest)) {
        char* open = strchr(statement, '{');
        char* close = strrchr(statement, '}');
        char* eq = strchr(statement, '=');
        node = cxpr_doc_ast_node_new(CXPR_DOC_AST_FUNCTION_DECL, span);
        (void)rest;
        if (!node) goto oom;
        if (open) {
            *open = '\0';
            if (!close || close < open) goto syntax;
            *close = '\0';
            node->name = cxpr_strdup(cxpr_doc_ast_trim_in_place(statement + 2));
            ok = node->name != NULL &&
                 cxpr_doc_ast_parse_function_body(
                     parser,
                     node,
                     open + 1,
                     start_offset + (size_t)(open + 1 - statement));
        } else if (eq) {
            char* rhs;
            *eq = '\0';
            node->name = cxpr_strdup(cxpr_doc_ast_trim_in_place(statement + 2));
            rhs = cxpr_doc_ast_trim_in_place(eq + 1);
            node->text = cxpr_strdup(rhs);
            if (*rhs != '{' && !cxpr_doc_ast_has_top_level_comma(rhs)) {
                node->expression = cxpr_doc_ast_parse_expr(
                    node->text, span.start.line, span.start.column + 1u, parser->err);
                ok = node->name && node->text && node->expression;
            } else {
                ok = node->name && node->text;
            }
        } else {
            goto syntax;
        }
    } else if (statement[0] == '$') {
        char* after = statement + 1;
        while (*after && isspace((unsigned char)*after)) after++;
        if (*after == '{') {
            char* close = strrchr(after, '}');
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_PARAM_BLOCK, span);
            if (!node) goto oom;
            if (!close || close < after) goto syntax;
            *close = '\0';
            ok = cxpr_doc_ast_parse_comma_or_line_decls(
                parser, node, CXPR_DOC_AST_PARAM_DECL, after + 1,
                start_offset + (size_t)(after + 1 - statement), true);
        } else {
            node = cxpr_doc_ast_node_new(CXPR_DOC_AST_PARAM_DECL, span);
            if (!node) goto oom;
            ok = cxpr_doc_ast_assign_name_expr(
                node, parser, statement, true, start_offset,
                span.start.line, span.start.column + 1u, parser->err);
        }
    } else if (strstr(statement, ":=")) {
        char* initial = cxpr_doc_ast_find_top_level_keyword(
            strstr(statement, ":=") + 2, "initial");
        node = cxpr_doc_ast_node_new(
            initial ? CXPR_DOC_AST_INITIAL_STATE_UPDATE : CXPR_DOC_AST_STATE_UPDATE,
            span);
        if (!node) goto oom;
        ok = initial
                 ? cxpr_doc_ast_assign_initial_state_update(
                       node, statement,
                       span.start.line, span.start.column + 1u, parser->err)
                 : cxpr_doc_ast_assign_name_update(
                       node, statement, span.start.line, span.start.column + 1u, parser->err);
    } else if (strchr(statement, '=')) {
        node = cxpr_doc_ast_node_new(CXPR_DOC_AST_BINDING, span);
        if (!node) goto oom;
        ok = cxpr_doc_ast_assign_name_expr(
            node, parser, statement, false, start_offset,
            span.start.line, span.start.column + 1u, parser->err);
    } else {
        goto syntax;
    }

    if (!ok) {
        cxpr_doc_ast_node_free(node);
        free(owned);
        return false;
    }
    if ((parser->extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u) {
        cxpr_doc_ast_node_free(node);
        free(owned);
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_SYNTAX,
                                    "Model syntax requires CXPR_DOCUMENT_EXTENSION_MODEL",
                                    span.start.line, span.start.column + 1u);
        return false;
    }
    if (!cxpr_doc_ast_append_child(root, node)) {
        cxpr_doc_ast_node_free(node);
        goto oom;
    }
    free(owned);
    return true;

syntax:
    cxpr_doc_ast_node_free(node);
    free(owned);
    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_SYNTAX,
                                "Failed to parse document syntax",
                                span.start.line, span.start.column + 1u);
    return false;
oom:
    cxpr_doc_ast_node_free(node);
    free(owned);
    cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY,
                                "Out of memory", span.start.line, span.start.column + 1u);
    return false;
}

static bool cxpr_doc_ast_parse_host_statement(cxpr_doc_ast_parser* parser,
                                                   cxpr_doc_ast_node* root,
                                                   const char* text,
                                                   size_t start_offset,
                                                   size_t end_offset) {
    char* owned = cxpr_doc_ast_trim_copy(text, end_offset - start_offset);
    char* statement;
    char* kind = NULL;
    char* name = NULL;
    const char* body = NULL;
    cxpr_doc_ast_node* node;
    if (!owned) {
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return false;
    }
    statement = cxpr_doc_ast_trim_in_place(owned);
    if (!cxpr_doc_ast_parse_host_start(statement, &kind, &name, &body)) {
        free(owned);
        return cxpr_doc_ast_parse_statement(parser, root, text, start_offset, end_offset);
    }
    {
        char* close = strrchr((char*)body, '}');
        if (close) *close = '\0';
    }
    node = cxpr_doc_ast_node_new(
        CXPR_DOC_AST_HOST_BLOCK,
        cxpr_doc_ast_span(parser, start_offset, end_offset));
    if (!node) {
        free(kind);
        free(name);
        free(owned);
        cxpr_doc_ast_set_error(parser->err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return false;
    }
    node->name = kind;
    node->value = name;
    node->text = cxpr_strdup(body ? body : "");
    if (!node->text ||
        !cxpr_doc_ast_parse_host_fields(
            parser,
            node,
            node->text,
            start_offset + (size_t)(body ? body - statement : 0)) ||
        !cxpr_doc_ast_append_child(root, node)) {
        cxpr_doc_ast_node_free(node);
        free(owned);
        if (!parser->err || parser->err->code == CXPR_OK) {
            cxpr_doc_ast_set_error(
                parser->err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        }
        return false;
    }
    free(owned);
    return true;
}

cxpr_doc_ast* cxpr_doc_ast_parse(const char* source,
                                           const char* source_name,
                                           unsigned extensions,
                                           cxpr_error* err) {
    cxpr_doc_ast_parser parser;
    cxpr_doc_ast* ast;
    char* parse_source;
    size_t line_start = 0u;
    size_t statement_start = 0u;
    int brace_depth = 0;
    bool has_current = false;

    if (err) *err = (cxpr_error){0};
    if (!source) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_SYNTAX, "NULL document source", 0u, 0u);
        return NULL;
    }
    parse_source = cxpr_doc_ast_strip_comments(source);
    if (!parse_source) {
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return NULL;
    }

    ast = (cxpr_doc_ast*)calloc(1u, sizeof(*ast));
    if (!ast) {
        free(parse_source);
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return NULL;
    }
    ast->source_name = cxpr_strdup(source_name ? source_name : "");
    ast->source_text = cxpr_strdup(source);
    ast->extensions = extensions;
    parser.source = parse_source;
    parser.extensions = extensions;
    parser.length = strlen(parse_source);
    parser.err = err;
    ast->root = cxpr_doc_ast_node_new(
        CXPR_DOC_AST_FILE,
        cxpr_doc_ast_span(&parser, 0u, parser.length));
    if (!ast->source_name || !ast->source_text || !ast->root) {
        cxpr_doc_ast_free(ast);
        free(parse_source);
        cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
        return NULL;
    }

    for (size_t i = 0u; i <= parser.length; ++i) {
        bool at_end = i == parser.length;
        bool at_line = !at_end && parse_source[i] == '\n';
        if (!at_end && !at_line) continue;
        {
            size_t line_end = i;
            char* line = cxpr_doc_ast_trim_copy(
                parse_source + line_start, line_end - line_start);
            bool blank;
            if (!line) {
                cxpr_doc_ast_free(ast);
                free(parse_source);
                cxpr_doc_ast_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory", 0u, 0u);
                return NULL;
            }
            blank = line[0] == '\0' || line[0] == '#' || (line[0] == '/' && line[1] == '/');
            if (blank) {
                if (has_current && brace_depth <= 0) {
                    if (!cxpr_doc_ast_parse_host_statement(
                            &parser, ast->root, parse_source + statement_start,
                            statement_start, line_start > 0u ? line_start - 1u : line_start)) {
                        free(line);
                        cxpr_doc_ast_free(ast);
                        free(parse_source);
                        return NULL;
                    }
                    has_current = false;
                }
            } else {
                bool indented = isspace((unsigned char)parse_source[line_start]) != 0;
                if (has_current && brace_depth <= 0 && !indented) {
                    if (!cxpr_doc_ast_parse_host_statement(
                            &parser, ast->root, parse_source + statement_start,
                            statement_start, line_start > 0u ? line_start - 1u : line_start)) {
                        free(line);
                        cxpr_doc_ast_free(ast);
                        free(parse_source);
                        return NULL;
                    }
                    has_current = false;
                }
                if (!has_current) {
                    statement_start = line_start;
                    has_current = true;
                }
                brace_depth += cxpr_doc_ast_brace_delta(line);
            }
            free(line);
        }
        line_start = i + 1u;
    }
    if (has_current &&
        !cxpr_doc_ast_parse_host_statement(
            &parser, ast->root, parse_source + statement_start, statement_start, parser.length)) {
        cxpr_doc_ast_free(ast);
        free(parse_source);
        return NULL;
    }
    free(parse_source);
    if (err) err->code = CXPR_OK;
    return ast;
}

void cxpr_doc_ast_free(cxpr_doc_ast* ast) {
    if (!ast) return;
    cxpr_doc_ast_node_free(ast->root);
    free(ast->source_name);
    free(ast->source_text);
    free(ast);
}
