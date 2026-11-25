/**
 * @file tristate.h
 * @brief Kleene three-valued partial evaluation of boolean expressions.
 *
 * Given a boolean expression and a caller-supplied predicate that marks each
 * leaf reference as KNOWN or UNKNOWN, this lowers the expression to the ordinary
 * boolean expression that is true iff the original is *definitely* true under
 * Kleene three-valued logic (UNKNOWN leaves may take either value).
 *
 * The transform is purely structural boolean/arithmetic algebra: it has no
 * notion of what makes a leaf known or unknown. Callers encode that policy in
 * the classifier. This keeps cxpr free of any host- or domain-specific meaning
 * (timeframes, bars, positions, ...) while still providing the reusable logic.
 *
 * Because the set of unknown leaves is fixed when the classifier runs,
 * "definitely true" is itself an ordinary boolean function of the known leaves,
 * so the result is a plain AST requiring no three-valued evaluation at runtime.
 */
#ifndef CXPR_TRISTATE_H
#define CXPR_TRISTATE_H

#include <cxpr/expr/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Known-ness of a single leaf reference under the caller's policy. */
typedef enum {
    CXPR_TRISTATE_UNKNOWN = 0, /**< Value may differ from its last-known value. */
    CXPR_TRISTATE_KNOWN = 1     /**< Value is fully determined for this evaluation. */
} cxpr_tristate_knownness;

/**
 * @brief Classify one leaf reference as known or unknown.
 *
 * Invoked for reference atoms only: identifiers, `$param` variables, field and
 * chain accesses, producer accesses and function calls. Numeric and boolean
 * literals are always treated as known and are not passed to the classifier.
 *
 * @param leaf     Borrowed leaf AST node.
 * @param userdata Opaque caller context.
 * @return Whether @p leaf is known for this evaluation.
 */
typedef cxpr_tristate_knownness (*cxpr_tristate_leaf_fn)(const cxpr_expr_ast* leaf,
                                                        void* userdata);

/**
 * @brief Build the "definitely true" boolean expression under Kleene logic.
 *
 * @param expr     Boolean expression to lower.
 * @param classify Leaf classifier encoding the known/unknown policy.
 * @param userdata Opaque context passed to @p classify.
 * @return Newly allocated, constant-folded boolean AST, which is the literal
 *         `false` when the expression can never be definitely true given the
 *         unknown leaves. NULL on allocation failure. Caller owns the result.
 */
cxpr_expr_ast* cxpr_tristate_definitely_true(const cxpr_expr_ast* expr,
                                             cxpr_tristate_leaf_fn classify,
                                             void* userdata);

/**
 * @brief Report whether an expression's value is fully known.
 *
 * Returns true when every reference atom in @p expr is classified KNOWN and the
 * value therefore depends only on known data. Useful for a fixpoint that
 * classifies named bindings before lowering the expressions that reference
 * them.
 *
 * @param expr     Expression to inspect (boolean or numeric).
 * @param classify Leaf classifier encoding the known/unknown policy.
 * @param userdata Opaque context passed to @p classify.
 * @return true when @p expr is fully determined by known leaves.
 */
bool cxpr_tristate_is_known(const cxpr_expr_ast* expr,
                            cxpr_tristate_leaf_fn classify,
                            void* userdata);

/**
 * @brief Report whether the lowering is anything other than constant `false`.
 *
 * @param expr     Boolean expression to inspect.
 * @param classify Leaf classifier encoding the known/unknown policy.
 * @param userdata Opaque context passed to @p classify.
 * @return true when the expression can be definitely true for some assignment
 *         of the unknown leaves.
 */
bool cxpr_tristate_has_true_term(const cxpr_expr_ast* expr,
                                 cxpr_tristate_leaf_fn classify,
                                 void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_TRISTATE_H */
