/**
 * @file eval.h
 * @brief Public AST evaluation API for cxpr.
 */

#ifndef CXPR_EVAL_H
#define CXPR_EVAL_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Evaluate an AST to a typed runtime value.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast(const cxpr_expr_ast* ast, const cxpr_context* ctx,
                   const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err);
/**
 * @brief Evaluate an AST and require a numeric result.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_number(const cxpr_expr_ast* ast, const cxpr_context* ctx,
                          const cxpr_registry* reg, double* out_value, cxpr_error* err);
/**
 * @brief Evaluate an AST and require a boolean result.
 * @param ast AST to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output boolean on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_bool(const cxpr_expr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, bool* out_value, cxpr_error* err);
/**
 * @brief Compatibility helper for evaluating history at `ast[index_ast]`.
 *
 * This retains the legacy lookback name. It does not perform ordinary array
 * indexing; parse and evaluate a `CXPR_NODE_INDEX` expression for neutral
 * target-based dispatch.
 * @param ast Target AST to evaluate.
 * @param index_ast AST that evaluates to the desired lookback index.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast_at_lookback(const cxpr_expr_ast* ast,
                               const cxpr_expr_ast* index_ast,
                               const cxpr_context* ctx,
                               const cxpr_registry* reg,
                               cxpr_value* out_value,
                               cxpr_error* err);
/**
 * @brief Compatibility helper for evaluating history at `ast[offset]`.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_ast_at_offset(const cxpr_expr_ast* ast,
                             double lookback,
                             const cxpr_context* ctx,
                             const cxpr_registry* reg,
                             cxpr_value* out_value,
                             cxpr_error* err);
/**
 * @brief Evaluate an AST at one numeric lookback offset without constructing
 *        a temporary lookback AST.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry with a lookback resolver.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_at_offset(const cxpr_expr_ast* ast,
                         double lookback,
                         const cxpr_context* ctx,
                         const cxpr_registry* reg,
                         cxpr_value* out_value,
                         cxpr_error* err);
/**
 * @brief Evaluate an AST to a number at one numeric lookback offset.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_number_at_offset(const cxpr_expr_ast* ast,
                                    double lookback,
                                    const cxpr_context* ctx,
                                    const cxpr_registry* reg,
                                    double* out_value,
                                    cxpr_error* err);
/**
 * @brief Evaluate an AST to a bool at one numeric lookback offset.
 * @param ast Target AST to evaluate.
 * @param lookback Non-negative lookback offset.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output bool on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_eval_ast_bool_at_offset(const cxpr_expr_ast* ast,
                                  double lookback,
                                  const cxpr_context* ctx,
                                  const cxpr_registry* reg,
                                  bool* out_value,
                                  cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_EVAL_H */
