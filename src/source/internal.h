/**
 * @file internal.h
 * @brief Internal helpers shared by cxpr source-plan modules.
 *
 * This header is private to `libs/cxpr/src/source`. It keeps parsing logic in
 * `plan.c` separate from canonical rendering and lifecycle helpers in
 * `canonical.c` without exposing those helpers as public cxpr API.
 */

#ifndef CXPR_SOURCE_INTERNAL_H
#define CXPR_SOURCE_INTERNAL_H

#include <cxpr/source_plan.h>

/**
 * @brief Duplicate a NUL-terminated string.
 * @param[in] text Source string to copy.
 * @return Newly allocated copy, or NULL on allocation failure or NULL input.
 */
char* cxpr_source_plan_strdup(const char* text);

/**
 * @brief Clear storage owned by one source-plan node.
 * @param[in,out] node Node to clear. Child nodes are freed recursively.
 *
 * Borrowed AST pointers are not freed. The node is reset to an empty state with
 * `lookback_slot` set to `SIZE_MAX`.
 */
void cxpr_source_plan_node_clear(cxpr_source_plan_node* node);

/**
 * @brief Render a cxpr AST subtree into source-plan canonical text.
 * @param[in] ast AST subtree to render.
 * @param[out] out_text Receives an allocated string on success.
 * @return Non-zero on success, zero on unsupported AST or allocation failure.
 *
 * The caller owns `*out_text` on success and must free it with `free()`.
 */
int cxpr_source_plan_render_ast_canonical(const cxpr_ast* ast, char** out_text);

/**
 * @brief Rebuild canonical metadata for one source-plan node.
 * @param[in,out] plan Plan owning the canonical string.
 * @param[in,out] node Node whose `node_id` should be updated.
 * @return Non-zero on success, zero on rendering or hashing failure.
 *
 * This replaces `plan->canonical` with the canonical representation of @p node
 * and stores the stable hash in `node->node_id`.
 */
int cxpr_source_plan_finalize_node_canonical(
    cxpr_source_plan_ast* plan,
    cxpr_source_plan_node* node);

#endif /* CXPR_SOURCE_INTERNAL_H */
