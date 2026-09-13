/**
 * @file expr/compiled.h
 * @brief Public compiled-expression API for cxpr.
 */

#ifndef CXPR_EXPR_COMPILED_H
#define CXPR_EXPR_COMPILED_H

#include <cxpr/types.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile an AST into an executable program.
 * @param ast AST to compile.
 * @param reg Function registry used for resolution and codegen.
 * @param err Optional error output.
 * @return Newly allocated program on success, or NULL on failure.
 */
cxpr_expr_compiled* cxpr_expr_compile(const cxpr_expr_ast* ast, const cxpr_registry* reg, cxpr_error* err);
/**
 * @brief Evaluate a compiled program to a typed runtime value.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_expr_compiled_eval(const cxpr_expr_compiled* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_value* out_value, cxpr_error* err);
/**
 * @brief Evaluate a compiled program and require a numeric result.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output number on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_expr_compiled_eval_number(const cxpr_expr_compiled* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, double* out_value, cxpr_error* err);
/**
 * @brief Evaluate a compiled program and require a boolean result.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output boolean on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure or type mismatch.
 */
bool cxpr_expr_compiled_eval_bool(const cxpr_expr_compiled* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, bool* out_value, cxpr_error* err);
/**
 * @brief Free a compiled program.
 * @param prog Program to free. May be NULL.
 */
void cxpr_expr_compiled_free(cxpr_expr_compiled* prog);
/**
 * @brief Dump a human-readable representation of a compiled program.
 * @param prog Program to dump.
 * @param out Output stream to write to.
 */
void cxpr_expr_compiled_dump(const cxpr_expr_compiled* prog, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_EXPR_COMPILED_H */
