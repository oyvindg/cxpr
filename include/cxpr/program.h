/**
 * @file program.h
 * @brief Public compiled-program API for cxpr.
 */

#ifndef CXPR_PROGRAM_H
#define CXPR_PROGRAM_H

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
cxpr_program* cxpr_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_error* err);
/**
 * @brief Evaluate a compiled program to a typed runtime value.
 * @param prog Program to evaluate.
 * @param ctx Runtime context providing variables and params.
 * @param reg Function registry used during evaluation.
 * @param out_value Output value on success.
 * @param err Optional error output.
 * @return True on success, false on evaluation failure.
 */
bool cxpr_eval_program(const cxpr_program* prog, const cxpr_context* ctx,
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
bool cxpr_eval_program_number(const cxpr_program* prog, const cxpr_context* ctx,
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
bool cxpr_eval_program_bool(const cxpr_program* prog, const cxpr_context* ctx,
                            const cxpr_registry* reg, bool* out_value, cxpr_error* err);
/**
 * @brief Free a compiled program.
 * @param prog Program to free. May be NULL.
 */
void cxpr_program_free(cxpr_program* prog);
/**
 * @brief Dump a human-readable representation of a compiled program.
 * @param prog Program to dump.
 * @param out Output stream to write to.
 */
void cxpr_program_dump(const cxpr_program* prog, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PROGRAM_H */
