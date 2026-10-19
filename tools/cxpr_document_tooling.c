#include <cxpr/cxpr.h>
#include <cxpr/builtin.h>
#include <cxpr/doc_tooling.h>
#include "../src/model/internal.h"
#include "../src/registry/internal.h" // IWYU pragma: keep

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
    int failed;
} cxpr_json_writer;

static int cxpr_json_reserve(cxpr_json_writer* writer, size_t extra) {
    size_t required;
    size_t capacity;
    char* next;
    if (!writer || writer->failed) return 0;
    if (extra > SIZE_MAX - writer->length - 1u) {
        writer->failed = 1;
        return 0;
    }
    required = writer->length + extra + 1u;
    if (required <= writer->capacity) return 1;
    capacity = writer->capacity ? writer->capacity : 1024u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    next = (char*)realloc(writer->data, capacity);
    if (!next) {
        writer->failed = 1;
        return 0;
    }
    writer->data = next;
    writer->capacity = capacity;
    return 1;
}

static int cxpr_json_putc(int ch, cxpr_json_writer* writer) {
    if (!cxpr_json_reserve(writer, 1u)) return EOF;
    writer->data[writer->length++] = (char)ch;
    writer->data[writer->length] = '\0';
    return ch;
}

static int cxpr_json_puts(const char* text, cxpr_json_writer* writer) {
    size_t length = text ? strlen(text) : 0u;
    if (!cxpr_json_reserve(writer, length)) return EOF;
    if (length) memcpy(writer->data + writer->length, text, length);
    writer->length += length;
    writer->data[writer->length] = '\0';
    return 0;
}

static int cxpr_json_printf(cxpr_json_writer* writer, const char* format, ...) {
    va_list args;
    va_list copy;
    int length;
    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0u, format, copy);
    va_end(copy);
    if (length < 0 || !cxpr_json_reserve(writer, (size_t)length)) {
        va_end(args);
        if (writer) writer->failed = 1;
        return -1;
    }
    (void)vsnprintf(writer->data + writer->length,
                    writer->capacity - writer->length,
                    format,
                    args);
    va_end(args);
    writer->length += (size_t)length;
    return length;
}

static char* cxpr_json_take(cxpr_json_writer* writer) {
    char* result;
    if (!writer || writer->failed) {
        if (writer) free(writer->data);
        return NULL;
    }
    if (!writer->data) {
        writer->data = (char*)malloc(1u);
        if (!writer->data) return NULL;
        writer->data[0] = '\0';
    }
    result = writer->data;
    writer->data = NULL;
    return result;
}

/* Existing emitters intentionally keep their stream-shaped calls. These
 * aliases target the portable growable writer rather than a libc FILE. */
static _Thread_local cxpr_json_writer* tooling_output;
#undef stdout
#define stdout tooling_output
#define fputc(ch, stream) cxpr_json_putc((ch), (stream))
#define fputs(text, stream) cxpr_json_puts((text), (stream))
#define fprintf(stream, ...) cxpr_json_printf((stream), __VA_ARGS__)

static void json_string(const char* text) {
    fputc('"', stdout);
    for (; text && *text; ++text) {
        unsigned char ch = (unsigned char)*text;
        switch (ch) {
        case '\\': fputs("\\\\", stdout); break;
        case '"': fputs("\\\"", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (ch < 0x20u) {
                fprintf(stdout, "\\u%04x", (unsigned)ch);
            } else {
                fputc(ch, stdout);
            }
            break;
        }
    }
    fputc('"', stdout);
}

static void recover_error_location(cxpr_error* err, const char* source) {
    const char* marker;
    const char* start;
    const char* end;
    size_t offset;
    if (!err || !source || !err->message) return;
    marker = strstr(err->message, "operand '");
    if (!marker) marker = strstr(err->message, "argument '");
    if (!marker) marker = strstr(err->message, "expression '");
    if (!marker) marker = strstr(err->message, "unknown symbol '");
    if (!marker) marker = strstr(err->message, "runtime symbol '");
    if (!marker) return;
    start = strchr(marker, '\'');
    if (!start) return;
    start++;
    end = strchr(start, '\'');
    if (!end || end == start) return;
    for (offset = 0u; source[offset]; ++offset) {
        size_t len = (size_t)(end - start);
        int before_is_identifier;
        int after_is_identifier;
        if (strncmp(source + offset, start, len) != 0) continue;
        before_is_identifier = offset > 0u &&
            (isalnum((unsigned char)source[offset - 1u]) ||
             source[offset - 1u] == '_' || source[offset - 1u] == '$');
        after_is_identifier =
            isalnum((unsigned char)source[offset + len]) ||
            source[offset + len] == '_' || source[offset + len] == '$';
        if (!before_is_identifier && !after_is_identifier) {
            size_t i;
            err->position = offset;
            err->line = 1u;
            err->column = 0u;
            for (i = 0u; i < offset; ++i) {
                if (source[i] == '\n') { err->line++; err->column = 0u; }
                else err->column++;
            }
            return;
        }
    }
}

static const char* node_kind_name(cxpr_doc_ast_kind kind) {
    switch (kind) {
    case CXPR_DOC_AST_FILE: return "file";
    case CXPR_DOC_AST_HOST_BLOCK: return "hostBlock";
    case CXPR_DOC_AST_HOST_FIELD: return "hostField";
    case CXPR_DOC_AST_MODEL_DECL: return "model";
    case CXPR_DOC_AST_USE: return "use";
    case CXPR_DOC_AST_INPUT_DECL: return "input";
    case CXPR_DOC_AST_INPUT_BLOCK: return "inputBlock";
    case CXPR_DOC_AST_PARAM_DECL: return "param";
    case CXPR_DOC_AST_PARAM_BLOCK: return "paramBlock";
    case CXPR_DOC_AST_FUNCTION_DECL: return "function";
    case CXPR_DOC_AST_FUNCTION_BODY: return "functionBody";
    case CXPR_DOC_AST_LOCAL_BINDING: return "local";
    case CXPR_DOC_AST_RETURN: return "return";
    case CXPR_DOC_AST_STATE_DECL: return "state";
    case CXPR_DOC_AST_STATE_BLOCK: return "stateBlock";
    case CXPR_DOC_AST_STATE_UPDATE: return "stateUpdate";
    case CXPR_DOC_AST_BINDING: return "binding";
    case CXPR_DOC_AST_OUTPUT_DECL: return "output";
    case CXPR_DOC_AST_OUTPUT_BLOCK: return "outputBlock";
    case CXPR_DOC_AST_OUTPUT_STATE_UPDATE: return "outputStateUpdate";
    case CXPR_DOC_AST_ANONYMOUS_OUTPUT: return "anonymousOutput";
    case CXPR_DOC_AST_METADATA: return "metadata";
    case CXPR_DOC_AST_ASSERT: return "assert";
    default: return "unknown";
    }
}

static const char* token_type(cxpr_doc_ast_kind kind) {
    switch (kind) {
    case CXPR_DOC_AST_HOST_BLOCK: return "block";
    case CXPR_DOC_AST_MODEL_DECL: return "assignment";
    case CXPR_DOC_AST_USE: return "import";
    case CXPR_DOC_AST_INPUT_DECL: return "input";
    case CXPR_DOC_AST_PARAM_DECL: return "parameter";
    case CXPR_DOC_AST_FUNCTION_DECL: return "function";
    case CXPR_DOC_AST_LOCAL_BINDING: return "variable";
    case CXPR_DOC_AST_STATE_DECL:
    case CXPR_DOC_AST_STATE_UPDATE:
    case CXPR_DOC_AST_OUTPUT_STATE_UPDATE:
        return "state";
    case CXPR_DOC_AST_BINDING:
        return "assignment";
    case CXPR_DOC_AST_OUTPUT_DECL:
        return "variable";
    case CXPR_DOC_AST_HOST_FIELD:
    case CXPR_DOC_AST_METADATA:
        return "namedArgument";
    default:
        return NULL;
    }
}

static size_t line_zero(cxpr_source_pos pos) {
    return pos.line > 0u ? pos.line - 1u : 0u;
}

static void emit_range(cxpr_source_span span) {
    fprintf(stdout,
            "\"startLine\":%zu,\"startChar\":%zu,\"endLine\":%zu,\"endChar\":%zu",
            line_zero(span.start),
            span.start.column,
            line_zero(span.end),
            span.end.column);
}

static int tooling_identifier_char(int ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '$';
}

static int span_find_name(const char* source,
                          cxpr_source_span span,
                          const char* name,
                          size_t* out_line,
                          size_t* out_col,
                          size_t* out_len,
                          int* out_prefixed_dollar,
                          size_t* out_offset) {
    const char* start;
    const char* end;
    const char* hit;
    if (!source || !name || !*name) return 0;
    start = source + span.start.offset;
    end = source + span.end.offset;
    if (end < start) return 0;
    hit = start;
    while (hit && hit < end) {
        hit = strstr(hit, name);
        if (!hit || hit >= end) break;
        if (hit + strlen(name) <= end) {
            size_t name_length = strlen(name);
            int identifier_name = tooling_identifier_char(name[0]) &&
                tooling_identifier_char(name[name_length - 1u]);
            int valid_start = !identifier_name || hit == source ||
                hit[-1] == '$' || !tooling_identifier_char(hit[-1]);
            int valid_end = !identifier_name || hit + name_length >= end ||
                !tooling_identifier_char(hit[name_length]);
            if (!valid_start || !valid_end) {
                hit++;
                continue;
            }
            size_t line = line_zero(span.start);
            size_t col = span.start.column;
            const char* cursor;
            for (cursor = start; cursor < hit; ++cursor) {
                if (*cursor == '\n') {
                    line++;
                    col = 0u;
                } else {
                    col++;
                }
            }
            *out_line = line;
            *out_col = col;
            *out_len = name_length;
            if (out_prefixed_dollar) {
                *out_prefixed_dollar = hit > start && hit[-1] == '$';
            }
            if (out_offset) *out_offset = (size_t)(hit - source);
            return 1;
        }
        hit++;
    }
    return 0;
}

static int is_outline_node(cxpr_doc_ast_kind kind) {
    switch (kind) {
    case CXPR_DOC_AST_HOST_BLOCK:
    case CXPR_DOC_AST_MODEL_DECL:
    case CXPR_DOC_AST_USE:
    case CXPR_DOC_AST_INPUT_DECL:
    case CXPR_DOC_AST_PARAM_DECL:
    case CXPR_DOC_AST_FUNCTION_DECL:
    case CXPR_DOC_AST_STATE_DECL:
    case CXPR_DOC_AST_STATE_UPDATE:
    case CXPR_DOC_AST_BINDING:
    case CXPR_DOC_AST_OUTPUT_DECL:
    case CXPR_DOC_AST_OUTPUT_STATE_UPDATE:
    case CXPR_DOC_AST_ANONYMOUS_OUTPUT:
        return 1;
    default:
        return 0;
    }
}

static void emit_outline_node(const cxpr_doc_ast_node* node, int* comma) {
    cxpr_doc_ast_kind kind = cxpr_doc_ast_node_kind(node);
    const char* name = cxpr_doc_ast_node_name(node);
    const char* text = cxpr_doc_ast_node_text(node);
    const char* value = cxpr_doc_ast_node_value(node);
    cxpr_source_span span = cxpr_doc_ast_node_span(node);
    if (!is_outline_node(kind)) return;
    if (*comma) fputc(',', stdout);
    *comma = 1;
    fputs("{\"kind\":", stdout);
    json_string(node_kind_name(kind));
    fputs(",\"name\":", stdout);
    json_string(name ? name : (text ? text : node_kind_name(kind)));
    fputs(",\"value\":", stdout);
    json_string(value ? value : "");
    fputc(',', stdout);
    emit_range(span);
    fputc('}', stdout);
}

static void emit_fold_node(const cxpr_doc_ast_node* node, int* comma) {
    cxpr_source_span span = cxpr_doc_ast_node_span(node);
    if (line_zero(span.end) <= line_zero(span.start)) return;
    if (cxpr_doc_ast_node_child_count(node) == 0u &&
        cxpr_doc_ast_node_kind(node) != CXPR_DOC_AST_HOST_BLOCK) {
        return;
    }
    if (*comma) fputc(',', stdout);
    *comma = 1;
    fputc('{', stdout);
    emit_range(span);
    fputc('}', stdout);
}

static void emit_token_node(const cxpr_doc_ast_node* node,
                            const char* source,
                            int* comma) {
    cxpr_doc_ast_kind kind = cxpr_doc_ast_node_kind(node);
    /* Import components are emitted individually by the source tokenizer. */
    if (kind == CXPR_DOC_AST_USE) return;
    const char* type = token_type(kind);
    const char* name = cxpr_doc_ast_node_name(node);
    cxpr_source_span span = cxpr_doc_ast_node_span(node);
    size_t line;
    size_t col;
    size_t len;
    size_t offset;
    int prefixed_dollar = 0;
    if (!type) return;
    if ((!name || !*name) && cxpr_doc_ast_node_text(node)) {
        name = cxpr_doc_ast_node_text(node);
    }
    /* A guessed fallback can extend beyond the source line and makes editors reject
       the complete semantic-token response. Only emit positions proven by the AST span. */
    if (!name || !*name ||
        !span_find_name(source, span, name, &line, &col, &len,
                        &prefixed_dollar, &offset)) return;
    if (kind == CXPR_DOC_AST_OUTPUT_DECL) {
        size_t cursor = offset + len;
        while (source[cursor] == ' ' || source[cursor] == '\t') cursor++;
        type = source[cursor] == '=' ? "assignment" : "variable";
    }
    if (prefixed_dollar) {
        type = kind == CXPR_DOC_AST_INPUT_DECL ? "input" : "parameter";
        col--;
        len++;
    }
    if (*comma) fputc(',', stdout);
    *comma = 1;
    fprintf(stdout, "{\"line\":%zu,\"start\":%zu,\"length\":%zu,\"type\":", line, col, len);
    json_string(type);
    fputc('}', stdout);
}

static void walk_outline(const cxpr_doc_ast_node* node, int* comma) {
    size_t count;
    if (!node) return;
    emit_outline_node(node, comma);
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        walk_outline(cxpr_doc_ast_node_child(node, i), comma);
    }
}

static void walk_folds(const cxpr_doc_ast_node* node, int* comma) {
    size_t count;
    if (!node) return;
    emit_fold_node(node, comma);
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        walk_folds(cxpr_doc_ast_node_child(node, i), comma);
    }
}

static void walk_tokens(const cxpr_doc_ast_node* node,
                        const char* source,
                        int* comma) {
    size_t count;
    if (!node) return;
    emit_token_node(node, source, comma);
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        walk_tokens(cxpr_doc_ast_node_child(node, i), source, comma);
    }
}

#include "language-core/generated/cxpr_language_spec.inc"

static int tooling_keyword(const char* word, size_t length) {
    for (size_t i = 0u; i < sizeof(tooling_keywords) / sizeof(tooling_keywords[0]); ++i) {
        if (strlen(tooling_keywords[i]) == length && strncmp(tooling_keywords[i], word, length) == 0) return 1;
    }
    return 0;
}

static int tooling_is_exported_input(const cxpr_doc_ast_node* node,
                                     const char* source,
                                     const char* word,
                                     size_t length) {
    size_t count;
    const char* name;
    const char* reference_word = word;
    size_t reference_length = length;
    int parameter_reference;
    if (!node) return 0;
    name = cxpr_doc_ast_node_name(node);
    parameter_reference = length > 0u && word[0] == '$';
    if (parameter_reference) {
        word++;
        length--;
    }
    if (cxpr_doc_ast_node_kind(node) == CXPR_DOC_AST_INPUT_DECL &&
        name && strlen(name) == length && strncmp(name, word, length) == 0) {
        size_t declaration_line;
        size_t declaration_col;
        size_t declaration_length;
        int declaration_is_parameter = 0;
        if (span_find_name(source, cxpr_doc_ast_node_span(node), name,
                           &declaration_line, &declaration_col, &declaration_length,
                           &declaration_is_parameter, NULL) &&
            parameter_reference == declaration_is_parameter) {
            return 1;
        }
    }
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        if (tooling_is_exported_input(
                cxpr_doc_ast_node_child(node, i), source,
                reference_word, reference_length)) {
            return 1;
        }
    }
    return 0;
}

static void emit_source_token(size_t line, size_t col, size_t length,
                              const char* type, int* comma) {
    if (!length) return;
    if (*comma) fputc(',', stdout);
    *comma = 1;
    fprintf(stdout, "{\"line\":%zu,\"start\":%zu,\"length\":%zu,\"type\":",
            line, col, length);
    json_string(type);
    fputc('}', stdout);
}

static int tooling_inside_metadata_block(const char* source, size_t offset) {
    int metadata_stack[128] = {0};
    size_t depth = 0u;
    size_t line_start = 0u;
    int quote = 0;
    for (size_t i = 0u; source && source[i] && i < offset; ++i) {
        int ch = (unsigned char)source[i];
        if (quote) {
            if (ch == quote && (i == 0u || source[i - 1u] != '\\')) quote = 0;
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; continue; }
        if (ch == '#' || (ch == '/' && source[i + 1u] == '/')) {
            while (source[i] && source[i] != '\n' && i < offset) i++;
            if (source[i] == '\n') line_start = i + 1u;
            continue;
        }
        if (ch == '\n') { line_start = i + 1u; continue; }
        if (ch == '{' && depth < 128u) {
            size_t cursor = line_start;
            while (cursor < i && isspace((unsigned char)source[cursor])) cursor++;
            metadata_stack[depth] = (depth > 0u && metadata_stack[depth - 1u]) ||
                (i - cursor >= 4u && strncmp(source + cursor, "out ", 4u) == 0) ||
                (i - cursor >= 6u && strncmp(source + cursor, "model ", 6u) == 0);
            depth++;
        } else if (ch == '}' && depth > 0u) {
            depth--;
        }
    }
    return depth > 0u && metadata_stack[depth - 1u];
}

/* Source-level facts which the document AST deliberately does not retain.
 * Kept in C so every editor transport sees identical lexical/comment spans. */
static void emit_source_tokens(const char* source,
                               const cxpr_doc_ast_node* root,
                               int* comma) {
    size_t i = 0u;
    size_t line = 0u;
    size_t col = 0u;
    int in_use = 0;
    int after_from = 0;
    int in_flat_input_list = 0;
    int expect_flat_input = 0;
    while (source && source[i]) {
        size_t start = i;
        size_t start_col = col;
        int ch = (unsigned char)source[i];
        if (ch == '\n') {
            i++; line++; col = 0u; in_use = 0; after_from = 0;
            in_flat_input_list = 0; expect_flat_input = 0;
            continue;
        }
        if (isspace(ch)) { i++; col++; continue; }
        if (ch == '#' || (ch == '/' && source[i + 1u] == '/')) {
            while (source[i] && source[i] != '\n') { i++; col++; }
            emit_source_token(line, start_col, i - start, "comment", comma);
            continue;
        }
        if (ch == '/' && source[i + 1u] == '*') {
            size_t token_line = line;
            i += 2u; col += 2u;
            while (source[i] && !(source[i] == '*' && source[i + 1u] == '/')) {
                if (source[i++] == '\n') { line++; col = 0u; } else col++;
            }
            if (source[i]) { i += 2u; col += 2u; }
            /* Multiline comments are emitted per first span for the neutral contract. */
            emit_source_token(token_line, start_col, i - start, "comment", comma);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            int quote = ch;
            i++; col++;
            while (source[i] && source[i] != '\n') {
                int current = (unsigned char)source[i++]; col++;
                if (current == quote && (i < 2u || source[i - 2u] != '\\')) break;
            }
            emit_source_token(line, start_col, i - start, "string", comma);
            continue;
        }
        if (in_use && !after_from &&
            (isalpha(ch) || ch == '_')) {
            size_t path_end = i;
            int has_slash = 0;
            while (isalnum((unsigned char)source[path_end]) ||
                   source[path_end] == '_' || source[path_end] == '.' ||
                   source[path_end] == '/' || source[path_end] == '-') {
                if (source[path_end] == '/') has_slash = 1;
                path_end++;
            }
            if (has_slash) {
                i = path_end;
                col += path_end - start;
                emit_source_token(line, start_col, path_end - start,
                                  "importPath", comma);
                continue;
            }
        }
        if (isalpha(ch) || ch == '_' || ch == '$') {
            size_t cursor;
            size_t length;
            const char* type;
            int begins_line = 1;
            size_t prefix = start;
            i++; col++;
            while (isalnum((unsigned char)source[i]) || source[i] == '_' || source[i] == '$') { i++; col++; }
            length = i - start;
            cursor = i;
            while (source[cursor] == ' ' || source[cursor] == '\t') cursor++;
            while (prefix > 0u && source[prefix - 1u] != '\n') {
                prefix--;
                if (source[prefix] != ' ' && source[prefix] != '\t') {
                    begins_line = 0;
                    break;
                }
            }
            if (length == 3u && strncmp(source + start, "use", 3u) == 0) {
                in_use = 1;
                type = "keyword";
            } else if (in_use && length == 4u && strncmp(source + start, "from", 4u) == 0) {
                after_from = 1;
                type = "keyword";
            } else if (in_use) {
                type = after_from ? "importPath" : "import";
            } else {
                int begins_line_assignment = begins_line;
                size_t line_start;
                line_start = start;
                while (line_start > 0u && source[line_start - 1u] != '\n') line_start--;
                while (line_start < start &&
                       (source[line_start] == ' ' || source[line_start] == '\t')) {
                    line_start++;
                }
                if (!begins_line_assignment &&
                    ((start - line_start == 4u &&
                      strncmp(source + line_start, "out ", 4u) == 0) ||
                     (start - line_start == 6u &&
                      strncmp(source + line_start, "state ", 6u) == 0))) {
                    begins_line_assignment = 1;
                }
                if (begins_line && length == 2u &&
                    strncmp(source + start, "in", 2u) == 0) {
                    in_flat_input_list = 1;
                    expect_flat_input = 1;
                }
                type = expect_flat_input && !(length == 2u &&
                        strncmp(source + start, "in", 2u) == 0) ? "input" :
                    length == 8u && strncmp(source + start, "optimize", 8u) == 0 &&
                    (source[cursor] == '=' || source[cursor] == '{') ? "namedArgument" :
                    tooling_keyword(source + start, length) ? "keyword" :
                    source[cursor] == '(' ? "function" :
                    source[cursor] == '=' ?
                        (tooling_inside_metadata_block(source, start) ? "namedArgument" :
                         begins_line_assignment ? "assignment" : "namedArgument") :
                    tooling_is_exported_input(root, source, source + start, length) ? "input" :
                    source[start] == '$' ? "parameter" :
                    "reference";
                if (expect_flat_input && strcmp(type, "input") == 0) {
                    expect_flat_input = 0;
                }
            }
            emit_source_token(line, start_col, length, type, comma);
            continue;
        }
        if (isdigit(ch) || (ch == '.' && isdigit((unsigned char)source[i + 1u]))) {
            i++; col++;
            while (isalnum((unsigned char)source[i]) || source[i] == '.' || source[i] == '_') { i++; col++; }
            emit_source_token(line, start_col, i - start, "number", comma);
            continue;
        }
        if (in_flat_input_list) {
            if (ch == ',') expect_flat_input = 1;
            else if (ch == '{' || ch == '=') {
                in_flat_input_list = 0;
                expect_flat_input = 0;
            }
        }
        i++; col++;
        emit_source_token(line, start_col, 1u,
            strchr("()[]{},.;:", ch) ? "punctuation" :
            ch == '=' ? (tooling_inside_metadata_block(source, start)
                ? "namedArgument" : "assignment") : "operator", comma);
    }
}

static int document_has_model(const cxpr_doc_ast* ast) {
    const cxpr_doc_ast_node* root = cxpr_doc_ast_root(ast);
    size_t count = cxpr_doc_ast_node_child_count(root);
    for (size_t i = 0u; i < count; ++i) {
        if (cxpr_doc_ast_node_kind(cxpr_doc_ast_node_child(root, i)) ==
            CXPR_DOC_AST_MODEL_DECL) {
            return 1;
        }
    }
    return 0;
}

static void collect_function_names(const cxpr_model* model,
                                   const char** names,
                                   size_t* count,
                                   size_t capacity) {
    if (!model || !names || !count) return;
    for (size_t group = 0u; group < 2u; ++group) {
        size_t expr_count = group == 0u
            ? cxpr_model_constant_count(model)
            : cxpr_model_binding_count(model);
        for (size_t i = 0u; i < expr_count; ++i) {
            const cxpr_expr_ast* expr = group == 0u
                ? cxpr_model_constant_expr(model, i)
                : cxpr_model_binding_expr(model, i);
            const char* used[128];
            size_t used_count = cxpr_expr_ast_functions_used(expr, used, 128u);
            for (size_t j = 0u; j < used_count && *count < capacity; ++j) {
                int duplicate = 0;
                for (size_t k = 0u; k < *count; ++k) {
                    if (strcmp(names[k], used[j]) == 0) duplicate = 1;
                }
                if (!duplicate) names[(*count)++] = used[j];
            }
        }
    }
}

static int typecheck_model_expressions(const cxpr_model* model,
                                       const cxpr_registry* registry,
                                       cxpr_error* err) {
    for (size_t group = 0u; model && group < 2u; ++group) {
        size_t count = group == 0u
            ? cxpr_model_constant_count(model)
            : cxpr_model_binding_count(model);
        for (size_t i = 0u; i < count; ++i) {
            const cxpr_expr_ast* expr = group == 0u
                ? cxpr_model_constant_expr(model, i)
                : cxpr_model_binding_expr(model, i);
            /* Typed buffers deliberately have no initializer expression. */
            if (!expr) continue;
            if (!cxpr_typecheck(expr, registry, NULL, err)) return 0;
        }
    }
    return 1;
}

static void tooling_register_functions(cxpr_registry* registry) {
    if (!registry) return;
    cxpr_register_defaults(registry);
    cxpr_registry_add_ast(registry, "resample", cxpr_eval_bound_resample,
                          2u, 2u, CXPR_VALUE_NUMBER, NULL, NULL);
}

typedef struct tooling_requirement {
    const char* source;
    char interval[CXPR_RESAMPLE_CANONICAL_CAPACITY];
    int64_t duration_ns;
    const char* result_type;
    size_t line;
    size_t column;
} tooling_requirement;

static void tooling_requirement_position(const char* source,
                                         size_t* search_offset,
                                         size_t* line,
                                         size_t* column) {
    const char* hit = strstr(source + *search_offset, "resample");
    size_t offset = hit ? (size_t)(hit - source) : 0u;
    *line = 0u;
    *column = 0u;
    for (size_t i = 0u; i < offset; ++i) {
        if (source[i] == '\n') { (*line)++; *column = 0u; }
        else (*column)++;
    }
    if (hit) *search_offset = offset + strlen("resample");
}

static void tooling_collect_requirements_ast(const cxpr_expr_ast* ast,
                                             const char* document_source,
                                             size_t* search_offset,
                                             tooling_requirement* out,
                                             size_t* count,
                                             size_t capacity) {
    if (!ast || !out || !count) return;
    if (cxpr_expr_ast_kind_of(ast) == CXPR_NODE_FUNCTION_CALL &&
        cxpr_expr_ast_call_name(ast) &&
        strcmp(cxpr_expr_ast_call_name(ast), "resample") == 0) {
        cxpr_resample_call call = {0};
        cxpr_error err = {0};
        if (*count < capacity && cxpr_resample_call_parse(ast, &call, &err) &&
            call.source && cxpr_expr_ast_kind_of(call.source) == CXPR_NODE_IDENTIFIER) {
            const char* source_name = cxpr_expr_ast_identifier_name(call.source);
            int duplicate = 0;
            for (size_t i = 0u; i < *count; ++i) {
                if (out[i].duration_ns == call.every.duration_ns &&
                    strcmp(out[i].source, source_name) == 0) duplicate = 1;
            }
            if (!duplicate) {
                tooling_requirement* requirement = &out[(*count)++];
                *requirement = (tooling_requirement){
                    .source = source_name,
                    .duration_ns = call.every.duration_ns,
                    .result_type = "number",
                };
                snprintf(requirement->interval, sizeof(requirement->interval), "%s",
                         call.every.canonical);
                tooling_requirement_position(document_source, search_offset,
                                               &requirement->line, &requirement->column);
            }
        }
    }
    switch (cxpr_expr_ast_kind_of(ast)) {
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0u; i < cxpr_expr_ast_call_arg_count(ast); ++i)
            tooling_collect_requirements_ast(cxpr_expr_ast_call_arg(ast, i), document_source,
                                             search_offset, out, count, capacity);
        break;
    case CXPR_NODE_BINARY_OP:
        tooling_collect_requirements_ast(cxpr_expr_ast_binary_left(ast), document_source,
                                         search_offset, out, count, capacity);
        tooling_collect_requirements_ast(cxpr_expr_ast_binary_right(ast), document_source,
                                         search_offset, out, count, capacity);
        break;
    case CXPR_NODE_UNARY_OP:
        tooling_collect_requirements_ast(cxpr_expr_ast_unary_operand(ast), document_source,
                                         search_offset, out, count, capacity);
        break;
    case CXPR_NODE_INDEX:
        tooling_collect_requirements_ast(cxpr_expr_ast_index_target(ast), document_source,
                                         search_offset, out, count, capacity);
        tooling_collect_requirements_ast(cxpr_expr_ast_index_expression(ast), document_source,
                                         search_offset, out, count, capacity);
        break;
    case CXPR_NODE_TERNARY:
        tooling_collect_requirements_ast(cxpr_expr_ast_ternary_condition(ast), document_source,
                                         search_offset, out, count, capacity);
        tooling_collect_requirements_ast(cxpr_expr_ast_ternary_true(ast), document_source,
                                         search_offset, out, count, capacity);
        tooling_collect_requirements_ast(cxpr_expr_ast_ternary_false(ast), document_source,
                                         search_offset, out, count, capacity);
        break;
    default: break;
    }
}

char* cxpr_document_analyze_json(const char* input, const char* source_name) {
    cxpr_json_writer writer = {0};
    char* json;
    char* source;
    cxpr_error err = {0};
    cxpr_doc_ast* ast;
    cxpr_model* model = NULL;
    cxpr_error semantic_err = {0};
    int semantic_ok = 1;
    const char* function_names[256];
    size_t function_count = 0u;
    tooling_requirement requirements[128];
    size_t requirement_count = 0u;
    size_t requirement_search_offset = 0u;
    int comma = 0;

    if (!input) input = "";
    if (!source_name) source_name = "<memory>";
    source = (char*)malloc(strlen(input) + 1u);
    tooling_output = &writer;
    if (source) memcpy(source, input, strlen(input) + 1u);
    if (!source) {
        free(source);
        tooling_output = NULL;
        return NULL;
    }

    ast = cxpr_doc_ast_parse(
        source,
        source_name,
        CXPR_DOC_EXTENSION_MODEL,
        &err);
    if (!ast) {
        fputs("{\"ok\":false,\"error\":", stdout);
        json_string(err.message[0] ? err.message : "Failed to parse document");
        fprintf(stdout, ",\"line\":%zu,\"column\":%zu}\n", err.line, err.column);
        free(source);
        tooling_output = NULL;
        return cxpr_json_take(&writer);
    }

    model = cxpr_model_parse(source, &semantic_err);
    if (!model) {
        semantic_ok = 0;
    } else if (document_has_model(ast)) {
        size_t use_count = cxpr_model_use_count(model);
        char** external_refs = use_count
            ? (char**)calloc(use_count, sizeof(*external_refs))
            : NULL;
        if (use_count == 0u || external_refs) {
            for (size_t i = 0u; i < use_count; ++i) {
                external_refs[i] = (char*)cxpr_model_use_alias(model, i);
            }
            semantic_ok = cxpr_model_validate_with_external_refs(
                model, external_refs, use_count, &semantic_err) ? 1 : 0;
        }
        free(external_refs);
        collect_function_names(model, function_names, &function_count, 256u);
        if (semantic_ok) {
            cxpr_registry* registry = cxpr_registry_new();
            if (!registry) {
                semantic_ok = 0;
                semantic_err.code = CXPR_ERR_OUT_OF_MEMORY;
                semantic_err.message = "Out of memory";
            } else {
                tooling_register_functions(registry);
                semantic_ok = typecheck_model_expressions(
                    model, registry, &semantic_err);
                cxpr_registry_free(registry);
            }
        }
    }

    if (model) {
        for (size_t i = 0u; i < cxpr_model_constant_count(model); ++i) {
            tooling_collect_requirements_ast(
                cxpr_model_constant_expr(model, i), source, &requirement_search_offset,
                requirements, &requirement_count, 128u);
        }
        for (size_t i = 0u; i < cxpr_model_binding_count(model); ++i) {
            tooling_collect_requirements_ast(
                cxpr_model_binding_expr(model, i), source, &requirement_search_offset,
                requirements, &requirement_count, 128u);
        }
    }

    recover_error_location(&semantic_err, source);
    fputs("{\"ok\":true,\"outline\":[", stdout);
    walk_outline(cxpr_doc_ast_root(ast), &comma);
    fputs("],\"folds\":[", stdout);
    comma = 0;
    walk_folds(cxpr_doc_ast_root(ast), &comma);
    fputs("],\"requirements\":[", stdout);
    for (size_t i = 0u; i < requirement_count; ++i) {
        if (i) fputc(',', stdout);
        fputs("{\"kind\":\"resample\",\"source\":", stdout);
        json_string(requirements[i].source);
        fputs(",\"interval\":", stdout);
        json_string(requirements[i].interval);
        fprintf(stdout, ",\"durationNs\":\"%lld\",\"resultType\":",
                (long long)requirements[i].duration_ns);
        json_string(requirements[i].result_type);
        fprintf(stdout,
                ",\"range\":{\"startLine\":%zu,\"startChar\":%zu,"
                "\"endLine\":%zu,\"endChar\":%zu}}",
                requirements[i].line, requirements[i].column,
                requirements[i].line,
                requirements[i].column + strlen("resample"));
    }
    fputs("],\"tokens\":[", stdout);
    comma = 0;
    walk_tokens(cxpr_doc_ast_root(ast), source, &comma);
    fputs("],\"sourceTokens\":[", stdout);
    comma = 0;
    emit_source_tokens(source, cxpr_doc_ast_root(ast), &comma);
    fputs("],\"functions\":[", stdout);
    {
        cxpr_registry* registry = cxpr_registry_new();
        tooling_register_functions(registry);
        int function_json_comma = 0;
        for (size_t i = 0u; i < cxpr_model_function_count(model); ++i) {
            char* declaration = NULL;
            const char* source_text = cxpr_model_function_source(model, i);
            const char* open_paren;
            char function_name[256];
            size_t function_name_len;
            if (!source_text) continue;
            open_paren = strchr(source_text, '(');
            if (!open_paren) continue;
            function_name_len = (size_t)(open_paren - source_text);
            while (function_name_len > 0u &&
                   (source_text[function_name_len - 1u] == ' ' || source_text[function_name_len - 1u] == '\t')) {
                function_name_len--;
            }
            if (function_name_len == 0u || function_name_len >= sizeof(function_name)) continue;
            memcpy(function_name, source_text, function_name_len);
            function_name[function_name_len] = '\0';
            if (!cxpr_model_function_declaration_source(model, i, &declaration)) {
                const char* open_brace = strchr(source_text, '{');
                size_t signature_len = open_brace
                    ? (size_t)(open_brace - source_text)
                    : strlen(source_text);
                while (signature_len > 0u && isspace((unsigned char)source_text[signature_len - 1u])) {
                    signature_len--;
                }
                declaration = (char*)malloc(signature_len + 4u);
                if (!declaration) continue;
                memcpy(declaration, "fn ", 3u);
                memcpy(declaration + 3u, source_text, signature_len);
                declaration[signature_len + 3u] = '\0';
            }
            if (function_json_comma) fputc(',', stdout);
            function_json_comma = 1;
            fputs("{\"name\":", stdout);
            json_string(function_name);
            fputs(",\"builtin\":false,\"minArgs\":0,\"maxArgs\":0,\"signature\":", stdout);
            json_string(declaration);
            fputc('}', stdout);
            free(declaration);
        }
        for (size_t i = 0u; i < model->record_function_count; ++i) {
            const cxpr_model_record_function* record = &model->record_functions[i];
            if (!record->name) continue;
            if (function_json_comma) fputc(',', stdout);
            function_json_comma = 1;
            fputs("{\"name\":", stdout);
            json_string(record->name);
            fputs(",\"builtin\":false,\"minArgs\":", stdout);
            fprintf(stdout, "%zu,\"maxArgs\":%zu,\"signature\":", record->param_count, record->param_count);
            fputs("\"", stdout);
            fputs(record->name, stdout);
            fputs("(", stdout);
            for (size_t j = 0u; j < record->param_count; ++j) {
                if (j > 0u) fputs(", ", stdout);
                fputs(record->params[j], stdout);
            }
            fputs(")\"}", stdout);
        }
        for (size_t i = 0u; i < function_count; ++i) {
            size_t min_args = 0u;
            size_t max_args = 0u;
            const cxpr_func_entry* entry = NULL;
            int builtin = cxpr_registry_lookup(
                registry, function_names[i], &min_args, &max_args) ? 1 : 0;
            for (size_t j = 0u; builtin && j < registry->count; ++j) {
                if (registry->entries[j].name &&
                    strcmp(registry->entries[j].name, function_names[i]) == 0) {
                    entry = &registry->entries[j];
                    break;
                }
            }
            if (function_json_comma) fputc(',', stdout);
            function_json_comma = 1;
            fputs("{\"name\":", stdout);
            json_string(function_names[i]);
            fprintf(stdout,
                    ",\"builtin\":%s,\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                    builtin ? "true" : "false", min_args, max_args);
            fputc('"', stdout);
            fputs(function_names[i], stdout);
            fputc('(', stdout);
            if (entry) {
                for (size_t j = 0u; j < entry->param_name_count; ++j) {
                    if (j) fputs(", ", stdout);
                    fputs(entry->param_names[j], stdout);
                }
            }
            fputs(")\"", stdout);
            fputc('}', stdout);
        }
        cxpr_registry_free(registry);
    }
    fputs("],\"diagnostics\":[", stdout);
    if (!semantic_ok) {
        fputs("{\"severity\":1,\"message\":", stdout);
        json_string(semantic_err.message ? semantic_err.message : "CXPR semantic validation failed");
        fprintf(stdout,
                ",\"line\":%zu,\"column\":%zu}",
                semantic_err.line > 0u ? semantic_err.line - 1u : 0u,
                semantic_err.column);
    }
    fputs("]}\n", stdout);

    cxpr_model_free(model);
    cxpr_doc_ast_free(ast);
    free(source);
    tooling_output = NULL;
    json = cxpr_json_take(&writer);
    return json;
}

static int tooling_offset_at(const char* source,
                             size_t line,
                             size_t column,
                             size_t* out_offset) {
    size_t current_line = 0u;
    size_t current_column = 0u;
    size_t offset = 0u;
    if (!source || !out_offset) return 0;
    while (source[offset] && current_line < line) {
        if (source[offset++] == '\n') current_line++;
    }
    if (current_line != line) return 0;
    while (source[offset] && source[offset] != '\n' && current_column < column) {
        offset++;
        current_column++;
    }
    if (current_column != column) return 0;
    *out_offset = offset;
    return 1;
}

static int tooling_word_char(int ch) {
    return isalnum((unsigned char)ch) || ch == '_' || ch == '$';
}

static void tooling_word_at(const char* source,
                            size_t offset,
                            size_t* out_start,
                            size_t* out_end) {
    size_t start = offset;
    size_t end = offset;
    while (start > 0u && tooling_word_char(source[start - 1u])) start--;
    while (source[end] && tooling_word_char(source[end])) end++;
    *out_start = start;
    *out_end = end;
}

static int tooling_name_equals(const char* name,
                               const char* source,
                               size_t start,
                               size_t end) {
    size_t length;
    size_t name_length;
    if (!name) return 0;
    if (source[start] == '$') start++;
    length = end - start;
    name_length = strcspn(name, "(");
    return name_length == length && strncmp(name, source + start, length) == 0;
}

static const cxpr_doc_ast_node* tooling_find_declaration(
    const cxpr_doc_ast_node* node,
    const char* source,
    size_t start,
    size_t end) {
    size_t count;
    if (!node) return NULL;
    if (tooling_name_equals(cxpr_doc_ast_node_name(node), source, start, end)) {
        return node;
    }
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        const cxpr_doc_ast_node* found = tooling_find_declaration(
            cxpr_doc_ast_node_child(node, i), source, start, end);
        if (found) return found;
    }
    return NULL;
}

static const char* tooling_symbol_kind(cxpr_doc_ast_kind kind) {
    switch (kind) {
    case CXPR_DOC_AST_INPUT_DECL: return "input";
    case CXPR_DOC_AST_PARAM_DECL: return "parameter";
    case CXPR_DOC_AST_FUNCTION_DECL: return "function";
    case CXPR_DOC_AST_LOCAL_BINDING: return "variable";
    case CXPR_DOC_AST_STATE_DECL:
    case CXPR_DOC_AST_STATE_UPDATE:
    case CXPR_DOC_AST_OUTPUT_STATE_UPDATE: return "state";
    case CXPR_DOC_AST_BINDING: return "binding";
    case CXPR_DOC_AST_OUTPUT_DECL: return "output";
    case CXPR_DOC_AST_USE: return "import";
    default: return "symbol";
    }
}

static const cxpr_func_entry* tooling_registry_entry(const cxpr_registry* registry,
                                                      const char* source,
                                                      size_t start,
                                                      size_t end) {
    if (!registry) return NULL;
    for (size_t i = 0u; i < registry->count; ++i) {
        if (tooling_name_equals(registry->entries[i].name, source, start, end)) {
            return &registry->entries[i];
        }
    }
    return NULL;
}

static const cxpr_function_info* tooling_builtin_at_word(
    const char* source, size_t start, size_t end,
    const cxpr_function_info* host_builtins, size_t host_builtin_count) {
    for (size_t i = 0u; i < cxpr_builtin_count(); ++i) {
        const cxpr_function_info* builtin = cxpr_builtin_at(i);
        if (builtin && !(builtin->flags & CXPR_BUILTIN_INTERNAL) &&
            tooling_name_equals(builtin->name, source, start, end)) return builtin;
    }
    for (size_t i = 0u; host_builtins && i < host_builtin_count; ++i) {
        if (host_builtins[i].name &&
            !(host_builtins[i].flags & CXPR_BUILTIN_INTERNAL) &&
            tooling_name_equals(host_builtins[i].name, source, start, end)) {
            return &host_builtins[i];
        }
    }
    return NULL;
}

static void tooling_emit_signature(const cxpr_func_entry* entry) {
    fputc('"', stdout);
    fputs(entry->name, stdout);
    fputc('(', stdout);
    for (size_t i = 0u; i < entry->param_name_count; ++i) {
        if (i) fputs(", ", stdout);
        fputs(entry->param_names[i], stdout);
    }
    fputs(")\"", stdout);
}

static const char* tooling_value_type_name(cxpr_value_type type) {
    switch (type) {
    case CXPR_VALUE_NUMBER: return "number";
    case CXPR_VALUE_BOOL: return "bool";
    case CXPR_VALUE_STRING: return "string";
    case CXPR_VALUE_STRUCT: return "struct";
    case CXPR_VALUE_ARRAY: return "array";
    case CXPR_VALUE_INT64: return "int64";
    case CXPR_VALUE_TIMESTAMP: return "timestamp";
    case CXPR_VALUE_DURATION: return "duration";
    case CXPR_VALUE_NULL: return "null";
    default: return "unknown";
    }
}

static void tooling_emit_model_signature(const cxpr_model* model) {
    int comma = 0;
    const char* name = cxpr_model_name(model);
    fputc('"', stdout);
    fputs(name ? name : "model", stdout);
    fputc('(', stdout);
    for (size_t i = 0u; i < cxpr_model_input_count(model); ++i) {
        const char* input_name = cxpr_model_input(model, i);
        if (!input_name) continue;
        if (comma) fputs(", ", stdout);
        fputs(input_name, stdout);
        comma = 1;
    }
    for (size_t i = 0u; i < cxpr_model_constant_count(model); ++i) {
        const char* param_name;
        if (!cxpr_model_constant_is_call_param(model, i)) continue;
        param_name = cxpr_model_constant_name(model, i);
        if (!param_name) continue;
        if (comma) fputs(", ", stdout);
        fputs(param_name, stdout);
        comma = 1;
    }
    fputs(")\"", stdout);
}

static char* tooling_model_documentation(const char* source,
                                         const cxpr_doc_ast_node* declaration) {
    cxpr_source_span span = cxpr_doc_ast_node_span(declaration);
    size_t limit = 0u;
    size_t capacity;
    size_t length = 0u;
    char* documentation;
    size_t current_line = 1u;
    while (source[limit] && current_line < span.start.line) {
        if (source[limit++] == '\n') current_line++;
    }
    capacity = limit + 1u;
    documentation = (char*)malloc(capacity);
    if (!documentation) return NULL;
    for (size_t i = 0u; i < limit;) {
        size_t line_end = i;
        size_t cursor;
        while (line_end < limit && source[line_end] != '\n') line_end++;
        cursor = i;
        while (cursor < line_end && isspace((unsigned char)source[cursor])) cursor++;
        if (cursor < line_end && source[cursor] == '#') {
            cursor++;
            while (cursor < line_end && isspace((unsigned char)source[cursor])) cursor++;
            if (length && cursor < line_end) documentation[length++] = ' ';
            while (cursor < line_end) documentation[length++] = source[cursor++];
        }
        i = line_end < limit ? line_end + 1u : limit;
    }
    documentation[length] = '\0';
    return documentation;
}

char* cxpr_document_resolve_with_builtins_json(
    const char* input,
    const char* source_name,
    size_t line,
    size_t column,
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count) {
    cxpr_json_writer writer = {0};
    cxpr_error err = {0};
    cxpr_doc_ast* ast = NULL;
    cxpr_registry* registry = NULL;
    const cxpr_doc_ast_node* declaration = NULL;
    const cxpr_func_entry* entry = NULL;
    const cxpr_function_info* builtin = NULL;
    cxpr_model* parsed_model = NULL;
    size_t offset;
    size_t start;
    size_t end;
    char* result;
    if (!input) input = "";
    if (!source_name) source_name = "<memory>";
    tooling_output = &writer;
    if (!tooling_offset_at(input, line, column, &offset)) {
        fputs("{\"ok\":false,\"error\":\"Position outside document\"}\n", stdout);
        tooling_output = NULL;
        return cxpr_json_take(&writer);
    }
    tooling_word_at(input, offset, &start, &end);
    ast = cxpr_doc_ast_parse(input, source_name, CXPR_DOC_EXTENSION_MODEL, &err);
    registry = cxpr_registry_new();
    tooling_register_functions(registry);
    if (ast) {
        declaration = tooling_find_declaration(
            cxpr_doc_ast_root(ast), input, start, end);
        if (declaration &&
            cxpr_doc_ast_node_kind(declaration) == CXPR_DOC_AST_MODEL_DECL) {
            cxpr_error model_err = {0};
            parsed_model = cxpr_model_parse(input, &model_err);
        }
    }
    {
        size_t call_cursor = end;
        while (input[call_cursor] == ' ' || input[call_cursor] == '\t') call_cursor++;
        if (input[call_cursor] == '(') {
            entry = tooling_registry_entry(registry, input, start, end);
            if (entry && strncmp(entry->name, "__cxpr_", 7u) == 0) entry = NULL;
            builtin = tooling_builtin_at_word(
                input, start, end, host_builtins, host_builtin_count);
        }
    }
    fputs("{\"ok\":true,\"resolution\":", stdout);
    if (start == end) {
        fputs("null", stdout);
    } else {
        fputs("{\"name\":", stdout);
        {
            char* name = (char*)malloc(end - start + 1u);
            if (!name) writer.failed = 1;
            else {
                memcpy(name, input + start, end - start);
                name[end - start] = '\0';
                json_string(name);
                free(name);
            }
        }
        fputs(",\"kind\":", stdout);
        json_string(entry || builtin || parsed_model ? "function" : declaration
            ? tooling_symbol_kind(cxpr_doc_ast_node_kind(declaration))
            : "unresolved");
        fprintf(stdout,
                ",\"range\":{\"start\":%zu,\"end\":%zu}", start, end);
        if (builtin) {
            fprintf(stdout,
                    ",\"type\":\"%s\",\"builtin\":true,\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                    tooling_value_type_name(builtin->return_type),
                    builtin->min_args,
                    builtin->max_args);
            json_string(builtin->signature);
            fputs(",\"documentation\":", stdout);
            json_string(builtin->documentation);
        } else if (entry) {
            fprintf(stdout,
                    ",\"type\":\"%s\",\"builtin\":true,\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                    entry->has_return_type
                        ? tooling_value_type_name(entry->return_type)
                        : "unknown",
                    entry->min_args, entry->max_args);
            tooling_emit_signature(entry);
        } else if (declaration) {
            cxpr_source_span span = cxpr_doc_ast_node_span(declaration);
            size_t definition_line = line_zero(span.start);
            size_t definition_col = span.start.column;
            size_t definition_len = end - start;
            const char* declaration_name = cxpr_doc_ast_node_name(declaration);
            (void)span_find_name(input, span, declaration_name,
                                 &definition_line, &definition_col, &definition_len,
                                 NULL, NULL);
            definition_len = end - start;
            fputs(",\"type\":\"unknown\"", stdout);
            if (parsed_model) {
                size_t arg_count = cxpr_model_input_count(parsed_model) +
                    cxpr_model_call_param_count(parsed_model);
                char* documentation = tooling_model_documentation(input, declaration);
                fprintf(stdout, ",\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                        arg_count, arg_count);
                tooling_emit_model_signature(parsed_model);
                if (documentation && documentation[0]) {
                    fputs(",\"documentation\":", stdout);
                    json_string(documentation);
                }
                free(documentation);
            } else if (cxpr_doc_ast_node_kind(declaration) == CXPR_DOC_AST_FUNCTION_DECL &&
                cxpr_doc_ast_node_name(declaration)) {
                fputs(",\"signature\":", stdout);
                json_string(cxpr_doc_ast_node_name(declaration));
            } else if (cxpr_doc_ast_node_expr(declaration) && declaration_name) {
                char* expression = cxpr_expr_ast_to_string(
                    cxpr_doc_ast_node_expr(declaration));
                if (expression) {
                    const char* parameter_prefix = input[start] == '$' ? "$" : "";
                    size_t signature_length = strlen(parameter_prefix) +
                        strlen(declaration_name) + strlen(expression) + 4u;
                    char* signature = (char*)malloc(signature_length);
                    if (!signature) {
                        writer.failed = 1;
                    } else {
                        snprintf(signature, signature_length, "%s%s = %s",
                                 parameter_prefix, declaration_name, expression);
                        fputs(",\"signature\":", stdout);
                        json_string(signature);
                        free(signature);
                    }
                    free(expression);
                }
            }
            fputs(",\"builtin\":false,\"definition\":{", stdout);
            fprintf(stdout,
                    "\"startLine\":%zu,\"startChar\":%zu,\"endLine\":%zu,\"endChar\":%zu",
                    definition_line, definition_col,
                    definition_line, definition_col + definition_len);
            fputc('}', stdout);
        } else {
            fputs(",\"type\":\"unknown\",\"builtin\":false", stdout);
            {
                cxpr_error model_err = {0};
                cxpr_model* model = cxpr_model_parse(input, &model_err);
                fputs(",\"imports\":[", stdout);
                if (model) {
                    int import_comma = 0;
                    for (size_t i = 0u; i < cxpr_model_use_count(model); ++i) {
                        const char* use_path = cxpr_model_use(model, i);
                        if (!use_path) continue;
                        if (import_comma) fputc(',', stdout);
                        import_comma = 1;
                        json_string(use_path);
                    }
                }
                fputc(']', stdout);
                cxpr_model_free(model);
            }
        }
        fputc('}', stdout);
    }
    fputs("}\n", stdout);
    cxpr_registry_free(registry);
    cxpr_model_free(parsed_model);
    cxpr_doc_ast_free(ast);
    tooling_output = NULL;
    result = cxpr_json_take(&writer);
    return result;
}

char* cxpr_document_resolve_json(const char* input,
                                 const char* source_name,
                                 size_t line,
                                 size_t column) {
    return cxpr_document_resolve_with_builtins_json(
        input, source_name, line, column, NULL, 0u);
}

static int tooling_prefix_matches(const char* name,
                                  const char* source,
                                  size_t start,
                                  size_t end) {
    size_t length = end - start;
    if (source[start] == '$') {
        start++;
        length--;
    }
    return length == 0u || strncmp(name, source + start, length) == 0;
}

static int tooling_internal_function_name(const char* name) {
    return name && strncmp(name, "__cxpr_", 7u) == 0;
}

static void tooling_emit_candidate(const char* name,
                                   const char* kind,
                                   int builtin,
                                   const cxpr_func_entry* entry,
                                   const cxpr_function_info* provided_info,
                                   int* comma) {
    const cxpr_function_info* info = provided_info
        ? provided_info
        : builtin ? cxpr_builtin_find(name) : NULL;
    if (*comma) fputc(',', stdout);
    *comma = 1;
    fputs("{\"name\":", stdout);
    json_string(name);
    fputs(",\"kind\":", stdout);
    json_string(kind);
    fprintf(stdout, ",\"builtin\":%s", builtin ? "true" : "false");
    if (info) {
        fprintf(stdout, ",\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                info->min_args, info->max_args);
        json_string(info->signature);
        fputs(",\"type\":", stdout);
        json_string(tooling_value_type_name(info->return_type));
        fputs(",\"parameters\":[", stdout);
        for (size_t i = 0u; i < info->param_count; ++i) {
            if (i) fputc(',', stdout);
            fputs("{\"name\":", stdout);
            json_string(info->param_names[i]);
            fputs(",\"type\":\"unknown\"}", stdout);
        }
        fputc(']', stdout);
        fputs(",\"documentation\":", stdout);
        json_string(info->documentation);
    } else if (entry) {
        fprintf(stdout, ",\"minArgs\":%zu,\"maxArgs\":%zu,\"signature\":",
                entry->min_args, entry->max_args);
        tooling_emit_signature(entry);
        fputs(",\"type\":", stdout);
        json_string(entry->has_return_type
                        ? tooling_value_type_name(entry->return_type)
                        : "unknown");
        fputs(",\"parameters\":[", stdout);
        for (size_t i = 0u; i < entry->param_name_count; ++i) {
            if (i) fputc(',', stdout);
            fputs("{\"name\":", stdout);
            json_string(entry->param_names[i]);
            fputs(",\"type\":", stdout);
            json_string(i < entry->arg_type_count
                            ? tooling_value_type_name(entry->arg_types[i])
                            : "unknown");
            fputc('}', stdout);
        }
        fputc(']', stdout);
    }
    fputc('}', stdout);
}

static void tooling_emit_declarations(const cxpr_doc_ast_node* node,
                                      const char* source,
                                      size_t prefix_start,
                                      size_t prefix_end,
                                      int* comma) {
    const char* name;
    size_t count;
    if (!node) return;
    name = cxpr_doc_ast_node_name(node);
    if (name && *name &&
        tooling_prefix_matches(name, source, prefix_start, prefix_end)) {
        const char* kind = tooling_symbol_kind(cxpr_doc_ast_node_kind(node));
        if (strcmp(kind, "symbol") != 0) {
            size_t name_length = strcspn(name, "(");
            char* candidate_name = (char*)malloc(name_length + 1u);
            if (candidate_name) {
                memcpy(candidate_name, name, name_length);
                candidate_name[name_length] = '\0';
                tooling_emit_candidate(candidate_name, kind, 0, NULL, NULL, comma);
                free(candidate_name);
            }
        }
    }
    count = cxpr_doc_ast_node_child_count(node);
    for (size_t i = 0u; i < count; ++i) {
        tooling_emit_declarations(cxpr_doc_ast_node_child(node, i), source,
                                  prefix_start, prefix_end, comma);
    }
}

static const char* tooling_completion_context(const char* source,
                                              size_t offset,
                                              size_t* active_argument) {
    size_t cursor = offset;
    int depth = 0;
    *active_argument = 0u;
    while (cursor > 0u) {
        int ch = source[--cursor];
        if (ch == ')') depth++;
        else if (ch == '(') {
            if (depth == 0) return "callArgument";
            depth--;
        } else if (ch == ',' && depth == 0) {
            (*active_argument)++;
        } else if (ch == '\n' && depth == 0) {
            break;
        }
    }
    {
        size_t line_start = offset;
        while (line_start > 0u && source[line_start - 1u] != '\n') line_start--;
        if (strstr(source + line_start, "use ") == source + line_start ||
            strstr(source + line_start, "use ") != NULL) return "import";
    }
    return "expression";
}

char* cxpr_document_complete_with_builtins_json(
    const char* input,
    const char* source_name,
    size_t line,
    size_t column,
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count) {
    cxpr_json_writer writer = {0};
    cxpr_error err = {0};
    cxpr_doc_ast* ast = NULL;
    cxpr_registry* registry = NULL;
    size_t offset;
    size_t start;
    size_t end;
    size_t active_argument;
    const char* context;
    int comma = 0;
    char* result;
    if (!input) input = "";
    if (!source_name) source_name = "<memory>";
    tooling_output = &writer;
    if (!tooling_offset_at(input, line, column, &offset)) {
        fputs("{\"ok\":false,\"error\":\"Position outside document\"}\n", stdout);
        tooling_output = NULL;
        return cxpr_json_take(&writer);
    }
    tooling_word_at(input, offset, &start, &end);
    end = offset;
    context = tooling_completion_context(input, offset, &active_argument);
    ast = cxpr_doc_ast_parse(input, source_name, CXPR_DOC_EXTENSION_MODEL, &err);
    registry = cxpr_registry_new();
    tooling_register_functions(registry);
    fputs("{\"ok\":true,\"context\":", stdout);
    json_string(context);
    fprintf(stdout, ",\"activeArgument\":%zu,\"prefix\":", active_argument);
    {
        char* prefix = (char*)malloc(end - start + 1u);
        if (!prefix) writer.failed = 1;
        else {
            memcpy(prefix, input + start, end - start);
            prefix[end - start] = '\0';
            json_string(prefix);
            free(prefix);
        }
    }
    fputs(",\"candidates\":[", stdout);
    if (ast) {
        tooling_emit_declarations(cxpr_doc_ast_root(ast), input, start, end, &comma);
    }
    for (size_t i = 0u; i < sizeof(tooling_keywords) / sizeof(tooling_keywords[0]); ++i) {
        if (tooling_prefix_matches(tooling_keywords[i], input, start, end)) {
            tooling_emit_candidate(tooling_keywords[i], "keyword", 0, NULL, NULL, &comma);
        }
    }
    if (registry) {
        for (size_t i = 0u; i < registry->count; ++i) {
            const cxpr_func_entry* entry = &registry->entries[i];
            if (cxpr_builtin_find(entry->name) ||
                tooling_internal_function_name(entry->name)) continue;
            if (entry->name && tooling_prefix_matches(
                    entry->name, input, start, end)) {
                tooling_emit_candidate(entry->name, "function", 1, entry, NULL, &comma);
            }
        }
    }
    for (size_t i = 0u; i < cxpr_builtin_count(); ++i) {
        const cxpr_function_info* builtin = cxpr_builtin_at(i);
        if (builtin && !(builtin->flags & CXPR_BUILTIN_INTERNAL) &&
            tooling_prefix_matches(builtin->name, input, start, end)) {
            tooling_emit_candidate(builtin->name, "function", 1, NULL, builtin, &comma);
        }
    }
    for (size_t i = 0u; host_builtins && i < host_builtin_count; ++i) {
        const cxpr_function_info* builtin = &host_builtins[i];
        if (!builtin->name || (builtin->flags & CXPR_BUILTIN_INTERNAL) ||
            cxpr_builtin_find(builtin->name)) continue;
        if (tooling_prefix_matches(builtin->name, input, start, end)) {
            tooling_emit_candidate(
                builtin->name, "function", 1, NULL, builtin, &comma);
        }
    }
    fputs("]}\n", stdout);
    cxpr_registry_free(registry);
    cxpr_doc_ast_free(ast);
    tooling_output = NULL;
    result = cxpr_json_take(&writer);
    return result;
}

char* cxpr_document_complete_json(const char* input,
                                  const char* source_name,
                                  size_t line,
                                  size_t column) {
    return cxpr_document_complete_with_builtins_json(
        input, source_name, line, column, NULL, 0u);
}

void cxpr_document_json_free(char* json) {
    free(json);
}

char* cxpr_document_query_functions_with_builtins_json(
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count) {
    cxpr_json_writer writer = {0};
    cxpr_registry* registry = cxpr_registry_new();
    int comma = 0;
    if (!registry) return NULL;
    tooling_register_functions(registry);
    tooling_output = &writer;
    fputc('[', stdout);
    for (size_t i = 0u; i < registry->count; ++i) {
        const cxpr_func_entry* entry = &registry->entries[i];
        if (cxpr_builtin_find(entry->name) ||
            tooling_internal_function_name(entry->name)) continue;
        tooling_emit_candidate(entry->name, "function", 1, entry, NULL, &comma);
    }
    for (size_t i = 0u; i < cxpr_builtin_count(); ++i) {
        const cxpr_function_info* builtin = cxpr_builtin_at(i);
        if (builtin && !(builtin->flags & CXPR_BUILTIN_INTERNAL)) {
            tooling_emit_candidate(builtin->name, "function", 1, NULL, builtin, &comma);
        }
    }
    for (size_t i = 0u; host_builtins && i < host_builtin_count; ++i) {
        const cxpr_function_info* builtin = &host_builtins[i];
        if (!builtin->name || (builtin->flags & CXPR_BUILTIN_INTERNAL) ||
            cxpr_builtin_find(builtin->name)) continue;
        tooling_emit_candidate(builtin->name, "function", 1, NULL, builtin, &comma);
    }
    fputs("]\n", stdout);
    tooling_output = NULL;
    cxpr_registry_free(registry);
    return cxpr_json_take(&writer);
}

char* cxpr_document_query_functions_json(void) {
    return cxpr_document_query_functions_with_builtins_json(NULL, 0u);
}
