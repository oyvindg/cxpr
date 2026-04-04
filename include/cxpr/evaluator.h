/**
 * @file evaluator.h
 * @brief Public evaluator for cxpr.
 */

#ifndef CXPR_EVALUATOR_H
#define CXPR_EVALUATOR_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new expression evaluator.
 * @param reg Registry used for expression evaluation.
 * @return Newly allocated expression evaluator, or NULL on allocation failure.
 */
cxpr_evaluator* cxpr_evaluator_new(const cxpr_registry* reg);
/**
 * @brief Free an expression evaluator.
 * @param evaluator Evaluator to free. May be NULL.
 */
void cxpr_evaluator_free(cxpr_evaluator* evaluator);

/**
 * @brief Compile expressions and resolve dependency order.
 * @param evaluator Expression evaluator to compile.
 * @param err Optional error output.
 * @return True on success, false on dependency or compile failure.
 */
bool cxpr_evaluator_compile(cxpr_evaluator* evaluator, cxpr_error* err);
/**
 * @brief Evaluate the compiled expression set in dependency order.
 * @param evaluator Compiled expression evaluator.
 * @param ctx Context used for inputs and stored results.
 * @param err Optional error output.
 */
void cxpr_evaluator_eval(cxpr_evaluator* evaluator, cxpr_context* ctx, cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_EVALUATOR_H */
