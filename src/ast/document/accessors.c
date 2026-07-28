/**
 * @file ast/document/accessors.c
 * @brief Read-only document AST accessors.
 */

#include "ast/document/internal.h"

const cxpr_doc_ast_node* cxpr_doc_ast_root(const cxpr_doc_ast* ast) {
    return ast ? ast->root : NULL;
}

const char* cxpr_doc_ast_source_name(const cxpr_doc_ast* ast) {
    return ast ? ast->source_name : NULL;
}

const char* cxpr_doc_ast_source(const cxpr_doc_ast* ast) {
    return ast ? ast->source_text : NULL;
}

unsigned cxpr_doc_ast_extensions(const cxpr_doc_ast* ast) {
    return ast ? ast->extensions : 0u;
}

cxpr_doc_ast_kind cxpr_doc_ast_node_kind(const cxpr_doc_ast_node* node) {
    return node ? node->kind : CXPR_DOC_AST_FILE;
}

cxpr_source_span cxpr_doc_ast_node_span(const cxpr_doc_ast_node* node) {
    cxpr_source_span empty = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    return node ? node->span : empty;
}

const char* cxpr_doc_ast_node_name(const cxpr_doc_ast_node* node) {
    return node ? node->name : NULL;
}

const char* cxpr_doc_ast_node_value(const cxpr_doc_ast_node* node) {
    return node ? node->value : NULL;
}

const char* cxpr_doc_ast_node_text(const cxpr_doc_ast_node* node) {
    return node ? node->text : NULL;
}

const cxpr_expr_ast* cxpr_doc_ast_node_expr(const cxpr_doc_ast_node* node) {
    return node ? node->expression : NULL;
}

size_t cxpr_doc_ast_node_child_count(const cxpr_doc_ast_node* node) {
    return node ? node->children.count : 0u;
}

const cxpr_doc_ast_node* cxpr_doc_ast_node_child(const cxpr_doc_ast_node* node,
                                                      size_t index) {
    if (!node || index >= node->children.count) return NULL;
    return node->children.items[index];
}

