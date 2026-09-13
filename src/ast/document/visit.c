/**
 * @file ast/document/visit.c
 * @brief Recursive document AST traversal.
 */

#include "ast/document/internal.h"

static cxpr_visit_control cxpr_doc_ast_visit_node(const cxpr_doc_ast_node* node,
                                                       cxpr_doc_ast_visit_fn fn,
                                                       void* userdata) {
    cxpr_visit_control control;
    if (!node || !fn) return CXPR_VISIT_CONTINUE;
    control = fn(node, userdata);
    if (control == CXPR_VISIT_STOP || control == CXPR_VISIT_SKIP_CHILDREN) return control;
    for (size_t i = 0u; i < node->children.count; ++i) {
        control = cxpr_doc_ast_visit_node(node->children.items[i], fn, userdata);
        if (control == CXPR_VISIT_STOP) return control;
    }
    return CXPR_VISIT_CONTINUE;
}

cxpr_visit_control cxpr_doc_ast_visit(const cxpr_doc_ast* ast,
                                           cxpr_doc_ast_visit_fn fn,
                                           void* userdata) {
    return cxpr_doc_ast_visit_node(ast ? ast->root : NULL, fn, userdata);
}
