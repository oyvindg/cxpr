#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* read_stdin(void) {
    char* data = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    int ch;
    while ((ch = fgetc(stdin)) != EOF) {
        if (len + 1u >= cap) {
            size_t next_cap = cap ? cap * 2u : 4096u;
            char* next = (char*)realloc(data, next_cap);
            if (!next) {
                free(data);
                return NULL;
            }
            data = next;
            cap = next_cap;
        }
        data[len++] = (char)ch;
    }
    if (len + 1u >= cap) {
        char* next = (char*)realloc(data, len + 1u);
        if (!next) {
            free(data);
            return NULL;
        }
        data = next;
    }
    data[len] = '\0';
    return data;
}

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
    case CXPR_DOC_AST_OUTPUT_DECL:
        return "assignment";
    case CXPR_DOC_AST_HOST_FIELD:
    case CXPR_DOC_AST_METADATA:
        return "property";
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

static int span_find_name(const char* source,
                          cxpr_source_span span,
                          const char* name,
                          size_t* out_line,
                          size_t* out_col,
                          size_t* out_len) {
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
            *out_len = strlen(name);
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
    const char* type = token_type(kind);
    const char* name = cxpr_doc_ast_node_name(node);
    cxpr_source_span span = cxpr_doc_ast_node_span(node);
    size_t line;
    size_t col;
    size_t len;
    if (!type) return;
    if (!span_find_name(source, span, name, &line, &col, &len)) {
        line = line_zero(span.start);
        col = span.start.column;
        len = name && *name ? strlen(name) : 1u;
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

int main(int argc, char** argv) {
    const char* source_name = "<stdin>";
    char* source;
    cxpr_error err = {0};
    cxpr_doc_ast* ast;
    int comma = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--source-name") == 0 && i + 1 < argc) {
            source_name = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            fputs("usage: cxpr_document_tooling [--source-name PATH] < file.cxpr\n", stdout);
            return 0;
        }
    }

    source = read_stdin();
    if (!source) {
        fputs("{\"ok\":false,\"error\":\"Out of memory\"}\n", stdout);
        return 1;
    }

    ast = cxpr_doc_ast_parse(
        source,
        source_name,
        CXPR_DOCUMENT_EXTENSION_MODEL,
        &err);
    if (!ast) {
        fputs("{\"ok\":false,\"error\":", stdout);
        json_string(err.message[0] ? err.message : "Failed to parse document");
        fprintf(stdout, ",\"line\":%zu,\"column\":%zu}\n", err.line, err.column);
        free(source);
        return 2;
    }

    fputs("{\"ok\":true,\"outline\":[", stdout);
    walk_outline(cxpr_doc_ast_root(ast), &comma);
    fputs("],\"folds\":[", stdout);
    comma = 0;
    walk_folds(cxpr_doc_ast_root(ast), &comma);
    fputs("],\"tokens\":[", stdout);
    comma = 0;
    walk_tokens(cxpr_doc_ast_root(ast), source, &comma);
    fputs("]}\n", stdout);

    cxpr_doc_ast_free(ast);
    free(source);
    return 0;
}
