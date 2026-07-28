/**
 * @file ast/document/accessors.c
 * @brief Read-only document AST accessors.
 */

#include "ast/document/internal.h"

const cxpr_document_ast_node* cxpr_document_ast_root(const cxpr_document_ast* ast) {
    return ast ? ast->root : NULL;
}

const char* cxpr_document_ast_source_name(const cxpr_document_ast* ast) {
    return ast ? ast->source_name : NULL;
}

const char* cxpr_document_ast_source_text(const cxpr_document_ast* ast) {
    return ast ? ast->source_text : NULL;
}

unsigned cxpr_document_ast_extensions(const cxpr_document_ast* ast) {
    return ast ? ast->extensions : 0u;
}

cxpr_document_ast_kind cxpr_document_ast_node_kind(const cxpr_document_ast_node* node) {
    return node ? node->kind : CXPR_DOCUMENT_AST_FILE;
}

cxpr_source_span cxpr_document_ast_node_span(const cxpr_document_ast_node* node) {
    cxpr_source_span empty = {{0u, 0u, 0u}, {0u, 0u, 0u}};
    return node ? node->span : empty;
}

const char* cxpr_document_ast_node_name(const cxpr_document_ast_node* node) {
    return node ? node->name : NULL;
}

const char* cxpr_document_ast_node_value(const cxpr_document_ast_node* node) {
    return node ? node->value : NULL;
}

const char* cxpr_document_ast_node_text(const cxpr_document_ast_node* node) {
    return node ? node->text : NULL;
}

const cxpr_ast* cxpr_document_ast_node_expression(const cxpr_document_ast_node* node) {
    return node ? node->expression : NULL;
}

size_t cxpr_document_ast_child_count(const cxpr_document_ast_node* node) {
    return node ? node->children.count : 0u;
}

const cxpr_document_ast_node* cxpr_document_ast_child(const cxpr_document_ast_node* node,
                                                      size_t index) {
    if (!node || index >= node->children.count) return NULL;
    return node->children.items[index];
}

