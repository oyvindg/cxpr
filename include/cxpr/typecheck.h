/**
 * @file typecheck.h
 * @brief Static expression typecheck API for cxpr.
 */

#ifndef CXPR_TYPECHECK_H
#define CXPR_TYPECHECK_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Infer and validate expression types with strict boolean positions.
 * @param ast AST to typecheck.
 * @param reg Optional registry used to resolve declared function return types.
 * @param out_type Optional inferred root type; omitted for unknown roots.
 * @param err Optional error output.
 * @return True on success, false on a known type mismatch.
 */
bool cxpr_typecheck(const cxpr_ast* ast, const cxpr_registry* reg,
                    cxpr_value_type* out_type, cxpr_error* err);

/**
 * @brief Typecheck an expression and require a boolean or unknown root type.
 * @param ast AST to typecheck.
 * @param reg Optional registry used to resolve declared function return types.
 * @param err Optional error output.
 * @return True when the expression can be used in a boolean position.
 */
bool cxpr_typecheck_bool_root(const cxpr_ast* ast, const cxpr_registry* reg,
                              cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_TYPECHECK_H */
