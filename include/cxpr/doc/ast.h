/**
 * @file doc/ast.h
 * @brief Read-only accessors for block-aware .cxpr document syntax trees.
 */

#ifndef CXPR_DOC_AST_H
#define CXPR_DOC_AST_H

#include <cxpr/ast/expression.h>
#include <cxpr/doc.h>
#include <cxpr/source.h>
#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque block-aware syntax tree for one parsed `.cxpr` document. */
typedef struct cxpr_doc_ast cxpr_doc_ast;

/** @brief Opaque node in a @ref cxpr_doc_ast tree. */
typedef struct cxpr_doc_ast_node cxpr_doc_ast_node;

/**
 * @brief Built-in document and model-extension syntax node kinds.
 *
 * The document AST is source-oriented: it preserves meaningful block shape,
 * statement order and source spans before lowering normalizes the tree into a
 * semantic @ref cxpr_model. Expression-bearing nodes keep expression syntax in
 * the existing @ref cxpr_expr_ast representation.
 */
typedef enum {
    /** @brief Root node for a parsed document. */
    CXPR_DOC_AST_FILE,

    /** @brief Host-defined block such as `project { ... }` or `book name { ... }`. */
    CXPR_DOC_AST_HOST_BLOCK,

    /** @brief Field inside a host block, including bare flags represented as `true`. */
    CXPR_DOC_AST_HOST_FIELD,

    /** @brief Model declaration, e.g. `model strategy`. */
    CXPR_DOC_AST_MODEL_DECL,

    /** @brief Import declaration, including aliases and grouped imports. */
    CXPR_DOC_AST_USE,

    /** @brief Single input declaration. */
    CXPR_DOC_AST_INPUT_DECL,

    /** @brief Input block containing input declarations. */
    CXPR_DOC_AST_INPUT_BLOCK,

    /** @brief Single parameter declaration with an expression default. */
    CXPR_DOC_AST_PARAM_DECL,

    /** @brief Parameter block containing parameter declarations. */
    CXPR_DOC_AST_PARAM_BLOCK,

    /** @brief Function declaration, either expression-bodied or block-bodied. */
    CXPR_DOC_AST_FUNCTION_DECL,

    /** @brief Function body containing local bindings and a return node. */
    CXPR_DOC_AST_FUNCTION_BODY,

    /** @brief Local binding inside a function body. */
    CXPR_DOC_AST_LOCAL_BINDING,

    /** @brief Function return expression or record-return text. */
    CXPR_DOC_AST_RETURN,

    /** @brief State declaration with an initial expression. */
    CXPR_DOC_AST_STATE_DECL,

    /** @brief State block containing state declarations. */
    CXPR_DOC_AST_STATE_BLOCK,

    /** @brief Staged state update using `:=`. */
    CXPR_DOC_AST_STATE_UPDATE,

    /** @brief State declaration and staged update using `name := expression initial expression`. */
    CXPR_DOC_AST_INITIAL_STATE_UPDATE,

    /** @brief Top-level calculated binding. */
    CXPR_DOC_AST_BINDING,

    /** @brief Output declaration or expression output. */
    CXPR_DOC_AST_OUTPUT_DECL,

    /** @brief Output block containing output declarations. */
    CXPR_DOC_AST_OUTPUT_BLOCK,

    /** @brief Combined staged state update and output declaration. */
    CXPR_DOC_AST_OUTPUT_STATE_UPDATE,

    /** @brief Anonymous output expression or record/model output. */
    CXPR_DOC_AST_ANONYMOUS_OUTPUT,

    /** @brief Metadata attached to a model, declaration, binding or output. */
    CXPR_DOC_AST_METADATA
} cxpr_doc_ast_kind;

/** @brief Visitor control result for @ref cxpr_doc_ast_visit. */
typedef enum {
    /** @brief Continue traversal normally. */
    CXPR_VISIT_CONTINUE,

    /** @brief Do not visit the current node's children, then continue. */
    CXPR_VISIT_SKIP_CHILDREN,

    /** @brief Stop traversal immediately. */
    CXPR_VISIT_STOP
} cxpr_visit_control;

/**
 * @brief Callback invoked once per visited document AST node.
 *
 * @param node Borrowed node pointer valid for the lifetime of the AST.
 * @param userdata Caller-provided context.
 * @return Traversal control directive.
 */
typedef cxpr_visit_control (*cxpr_doc_ast_visit_fn)(
    const cxpr_doc_ast_node* node,
    void* userdata);

/**
 * @brief Parse a `.cxpr` source buffer into a block-aware syntax tree.
 *
 * The returned tree owns its copied source text and expression subtrees. Model
 * syntax is accepted only when @p extensions includes
 * @ref CXPR_DOC_EXTENSION_MODEL.
 *
 * @param source NUL-terminated document source.
 * @param source_name Optional diagnostic/source label; copied when present.
 * @param extensions Bitmask of @ref cxpr_doc_extension values.
 * @param err Optional error output, reset on entry.
 * @return New AST on success, or NULL on syntax/allocation failure. Free with
 *         @ref cxpr_doc_ast_free.
 */
cxpr_doc_ast* cxpr_doc_ast_parse(const char* source,
                                 const char* source_name,
                                 unsigned extensions,
                                 cxpr_error* err);

/** @brief Free an AST returned by @ref cxpr_doc_ast_parse. */
void cxpr_doc_ast_free(cxpr_doc_ast* ast);

/**
 * @brief Lower a syntax tree into a host-facing parsed document.
 *
 * The input AST is borrowed and remains owned by the caller. The returned
 * document owns an independent syntax copy and semantic model.
 *
 * @param ast Parsed syntax tree to lower.
 * @param err Optional error output, reset on entry.
 * @return New document on success, or NULL on validation/allocation failure.
 *         Free with @ref cxpr_doc_free.
 */
cxpr_doc* cxpr_doc_ast_lower(const cxpr_doc_ast* ast, cxpr_error* err);

/**
 * @brief Return the owned syntax tree for a parsed document.
 *
 * The returned pointer is borrowed and remains valid until
 * @ref cxpr_doc_free is called.
 */
const cxpr_doc_ast* cxpr_doc_ast_view(const cxpr_doc* document);

/** @brief Return the root node of @p ast, or NULL for NULL. */
const cxpr_doc_ast_node* cxpr_doc_ast_root(const cxpr_doc_ast* ast);

/** @brief Return the source label copied into @p ast. */
const char* cxpr_doc_ast_source_name(const cxpr_doc_ast* ast);

/** @brief Return the full source text copied into @p ast. */
const char* cxpr_doc_ast_source(const cxpr_doc_ast* ast);

/** @brief Return the extension bitmask used when parsing @p ast. */
unsigned cxpr_doc_ast_extensions(const cxpr_doc_ast* ast);

/** @brief Return a node's kind. */
cxpr_doc_ast_kind cxpr_doc_ast_node_kind(const cxpr_doc_ast_node* node);

/** @brief Return a node's source span. The end position is exclusive. */
cxpr_source_span cxpr_doc_ast_node_span(const cxpr_doc_ast_node* node);

/**
 * @brief Return a node's primary name.
 *
 * The meaning depends on the node kind: examples include model names, symbol
 * names, host block kinds, host field keys and function signatures.
 */
const char* cxpr_doc_ast_node_name(const cxpr_doc_ast_node* node);

/**
 * @brief Return a node's secondary value, when present.
 *
 * Currently used by host block nodes for an optional host block name/label, as
 * in `book market_neutral { ... }`. Returns NULL when not applicable.
 */
const char* cxpr_doc_ast_node_value(const cxpr_doc_ast_node* node);

/**
 * @brief Return a node's preserved text payload, when present.
 *
 * The meaning depends on the node kind: examples include raw host block bodies,
 * metadata bodies, import text, original expression text and record-return text.
 */
const char* cxpr_doc_ast_node_text(const cxpr_doc_ast_node* node);

/**
 * @brief Return a borrowed expression AST attached to a node, when present.
 *
 * The returned expression is owned by the document AST and remains valid until
 * @ref cxpr_doc_ast_free is called.
 */
const cxpr_expr_ast* cxpr_doc_ast_node_expr(const cxpr_doc_ast_node* node);

/** @brief Return the number of child syntax nodes under @p node. */
size_t cxpr_doc_ast_node_child_count(const cxpr_doc_ast_node* node);

/** @brief Return child @p index of @p node, or NULL when out of range. */
const cxpr_doc_ast_node* cxpr_doc_ast_node_child(const cxpr_doc_ast_node* node,
                                                 size_t index);

/**
 * @brief Visit a document AST in pre-order.
 *
 * The visitor is called for the root before its children. Returning
 * @ref CXPR_VISIT_SKIP_CHILDREN skips only the current node's subtree; returning
 * @ref CXPR_VISIT_STOP stops traversal and is returned to the caller.
 */
cxpr_visit_control cxpr_doc_ast_visit(const cxpr_doc_ast* ast,
                                      cxpr_doc_ast_visit_fn fn,
                                      void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_DOC_AST_H */
