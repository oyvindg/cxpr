/**
 * @file expression.h
 * @brief Public expression evaluator API for cxpr.
 */

#ifndef CXPR_EXPRESSION_H
#define CXPR_EXPRESSION_H

#include <cxpr/types.h>
#include <cxpr/ast.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One expression definition for batch registration. */
typedef struct {
    const char* name;
    const char* expression;
} cxpr_expression_def;

/**
 * @brief Add one named expression.
 * @param evaluator Destination expression evaluator.
 * @param name Expression name.
 * @param expression Expression source string.
 * @param err Optional error output.
 * @return True on success, false on parse or validation failure.
 */
bool cxpr_expression_add(cxpr_evaluator* evaluator, const char* name,
                         const char* expression, cxpr_error* err);
/**
 * @brief Add multiple expressions in one call.
 * @param evaluator Destination expression evaluator.
 * @param defs Expression definition array.
 * @param count Number of definitions in `defs`.
 * @param err Optional error output.
 * @return True on success, false on parse or validation failure.
 */
bool cxpr_expressions_add(cxpr_evaluator* evaluator, const cxpr_expression_def* defs,
                          size_t count, cxpr_error* err);
/**
 * @brief Parse and analyze an expression set without compiling or evaluating it.
 * @param defs Expression definition array.
 * @param count Number of definitions in `defs`.
 * @param reg Optional registry used to resolve functions.
 * @param out_analysis Output array of length `count` receiving per-expression analysis.
 * @param out_eval_order Optional output array of length `count` receiving expression indices in dependency order.
 * @param err Optional error output.
 * @return True on success, false on parse, semantic-analysis, or dependency-cycle failure.
 */
bool cxpr_analyze_expressions(const cxpr_expression_def* defs, size_t count,
                           const cxpr_registry* reg,
                           cxpr_analysis* out_analysis,
                           size_t* out_eval_order,
                           cxpr_error* err);
/**
 * @brief Backward-compatible alias for `cxpr_evaluator_compile`.
 * @param evaluator Expression evaluator to compile.
 * @param err Optional error output.
 * @return True on success, false on dependency or compile failure.
 */
bool cxpr_expression_compile(cxpr_evaluator* evaluator, cxpr_error* err);
/**
 * @brief Backward-compatible alias for `cxpr_evaluator_eval`.
 * @param evaluator Compiled expression evaluator.
 * @param ctx Context used for inputs and stored results.
 * @param err Optional error output.
 */
void cxpr_expression_eval_all(cxpr_evaluator* evaluator, cxpr_context* ctx, cxpr_error* err);

/**
 * @brief Get one expression result as a typed value.
 * @param evaluator Expression evaluator to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Expression result, or a zero-like value on miss.
 */
cxpr_value cxpr_expression_get(const cxpr_evaluator* evaluator, const char* name, bool* found);
/**
 * @brief Get one expression result as a number.
 * @param evaluator Expression evaluator to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Numeric result, or `0.0` on miss or type mismatch.
 */
double cxpr_expression_get_double(const cxpr_evaluator* evaluator, const char* name, bool* found);
/**
 * @brief Get one expression result as a boolean.
 * @param evaluator Expression evaluator to query.
 * @param name Expression name.
 * @param found Optional success flag output.
 * @return Boolean result, or `false` on miss or type mismatch.
 */
bool cxpr_expression_get_bool(const cxpr_evaluator* evaluator, const char* name, bool* found);
/**
 * @brief Return expression names in evaluation order.
 * @param evaluator Compiled expression evaluator.
 * @param names Output array for expression names.
 * @param max_names Maximum number of names to write.
 * @return Number of names written or available in evaluation order.
 */
size_t cxpr_expression_eval_order(const cxpr_evaluator* evaluator,
                                  const char** names, size_t max_names);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_EXPRESSION_H */
