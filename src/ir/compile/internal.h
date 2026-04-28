/**
 * @file internal.h
 * @brief Internal helpers shared across IR compilation units.
 */
#ifndef CXPR_IR_COMPILE_INTERNAL_H
#define CXPR_IR_COMPILE_INTERNAL_H

#include "ast/internal.h"
#include "ir/internal.h"

/** @brief Resolve one local variable name to its slot index.
 * @param name Candidate local name.
 * @param local_names Local-name table.
 * @param local_count Number of entries in `local_names`.
 * @return Matching slot index, or `(size_t)-1` when not found.
 */
size_t cxpr_ir_local_index(const char* name, const char* const* local_names,
                           size_t local_count);
/** @brief Resolve one inlined substitution name through the current frame stack.
 * @param frame Current substitution frame.
 * @param name Candidate identifier name.
 * @param owner Optional output receiving the frame that owned the substitution.
 * @return Borrowed AST argument mapped to `name`, or NULL when no substitution exists.
 */
const cxpr_ast* cxpr_ir_subst_lookup(const cxpr_ir_subst_frame* frame, const char* name,
                                     const cxpr_ir_subst_frame** owner);
/** @brief Emit the most direct load opcode for a leaf expression when possible.
 * @param ast Leaf AST node to lower.
 * @param program Destination IR program.
 * @param local_names Local-name table.
 * @param local_count Number of entries in `local_names`.
 * @param subst Active substitution frame stack.
 * @param square True to emit the square-specialized load variant.
 * @param err Optional error output.
 * @return True on success, false when the node is not a supported direct-load leaf.
 */
bool cxpr_ir_emit_leaf_load(const cxpr_ast* ast, cxpr_ir_program* program,
                            const char* const* local_names, size_t local_count,
                            const cxpr_ir_subst_frame* subst, bool square,
                            cxpr_error* err);
/** @brief Check whether an AST subtree contains any string literals.
 * @param ast AST subtree to inspect.
 * @return True when any reachable node is a string literal.
 */
bool cxpr_ir_ast_contains_string_literal(const cxpr_ast* ast);
/** @brief Check whether one AST argument must stay on AST-overlay evaluation.
 * @param ast AST subtree to inspect.
 * @return True when runtime overlay passthrough is required.
 */
bool cxpr_ir_arg_needs_overlay_passthrough(const cxpr_ast* ast);
/** @brief Check whether a runtime call must fall back to AST overlay execution.
 * @param ast Function-call or producer-call AST node.
 * @return True when any argument shape requires overlay passthrough.
 */
bool cxpr_ir_runtime_call_needs_overlay_passthrough(const cxpr_ast* ast);
/** @brief Infer the scalar fast-path result kind for one AST subtree.
 * @param ast AST subtree to inspect.
 * @param reg Registry used to resolve function metadata.
 * @param depth Current recursion depth.
 * @return One of the `CXPR_IR_RESULT_*` constants.
 */
unsigned char cxpr_ir_infer_fast_result_kind(const cxpr_ast* ast, const cxpr_registry* reg,
                                             size_t depth);
/** @brief Lower one AST subtree into IR instructions.
 * @param ast AST subtree to compile.
 * @param program Destination IR program.
 * @param reg Registry used for call resolution.
 * @param local_names Local-name table.
 * @param local_count Number of entries in `local_names`.
 * @param subst Active substitution frame stack for inlined defined functions.
 * @param inline_depth Current inlining depth.
 * @param err Optional error output.
 * @return True on success, false on compilation failure.
 */
bool cxpr_ir_compile_node(const cxpr_ast* ast, cxpr_ir_program* program,
                          const cxpr_registry* reg,
                          const char* const* local_names, size_t local_count,
                          const cxpr_ir_subst_frame* subst,
                          size_t inline_depth,
                          cxpr_error* err);

#endif
