#ifndef CXPR_AST_DOCUMENT_INTERNAL_H
#define CXPR_AST_DOCUMENT_INTERNAL_H

#include <cxpr/ast/document.h>

/** @brief Growable child list owned by an internal document AST node. */
typedef struct {
    cxpr_document_ast_node** items;
    size_t count;
    size_t capacity;
} cxpr_document_ast_node_list;

struct cxpr_document_ast_node {
    cxpr_document_ast_kind kind;
    cxpr_source_span span;
    char* name;
    char* value;
    char* text;
    cxpr_expr_ast* expression;
    cxpr_document_ast_node_list children;
};

struct cxpr_document_ast {
    char* source_name;
    char* source_text;
    unsigned extensions;
    cxpr_document_ast_node* root;
};

#endif /* CXPR_AST_DOCUMENT_INTERNAL_H */
