#include <cxpr/cxpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* duplicate(const char* text) {
    const size_t size = strlen(text) + 1u;
    char* copy = (char*)malloc(size);
    if (copy) memcpy(copy, text, size);
    return copy;
}

static char* emit_history_leaf(const cxpr_expr_ast* ast,
                               unsigned offset,
                               void* userdata,
                               cxpr_error* err) {
    char text[160];
    const char* name = cxpr_expr_ast_kind_of(ast) == CXPR_NODE_IDENTIFIER
                           ? cxpr_expr_ast_identifier_name(ast)
                           : NULL;
    (void)userdata;
    if (!name || strcmp(name, "close") != 0) {
        if (err) {
            err->code = CXPR_ERR_UNKNOWN_IDENTIFIER;
            err->message = "generated history fixture only exposes close";
        }
        return NULL;
    }
    if (offset == 0u) {
        snprintf(text, sizeof(text), "close[cursor]");
    } else {
        snprintf(text, sizeof(text),
                 "(cursor >= %uu ? close[cursor - %uu] : NAN)", offset, offset);
    }
    return duplicate(text);
}

int main(int argc, char** argv) {
    cxpr_expr_parser* parser;
    cxpr_expr_ast* ast;
    cxpr_error err = {0};
    cxpr_c_target target = {
        .api_version = CXPR_C_TARGET_API_VERSION,
        .emit_leaf_at_offset = emit_history_leaf,
    };
    char* expression;
    FILE* out;
    if (argc != 2) return 2;
    parser = cxpr_expr_parser_new();
    ast = parser ? cxpr_expr_ast_parse(parser, "close > close[1]", &err) : NULL;
    expression = ast ? cxpr_expr_ast_to_c(ast, &target, &err) : NULL;
    out = expression ? fopen(argv[1], "wb") : NULL;
    if (!out) {
        fprintf(stderr, "%s\n", err.message ? err.message : "history codegen failed");
        free(expression);
        cxpr_expr_ast_free(ast);
        cxpr_expr_parser_free(parser);
        return 1;
    }
    fputs("#include <math.h>\n#include <stdbool.h>\n#include <stddef.h>\n", out);
    fprintf(out,
            "static bool generated_history_up(const double* close, size_t count, "
            "size_t cursor) {\n"
            "    if (!close || cursor >= count) return false;\n"
            "    return %s;\n"
            "}\n",
            expression);
    fclose(out);
    free(expression);
    cxpr_expr_ast_free(ast);
    cxpr_expr_parser_free(parser);
    return 0;
}
