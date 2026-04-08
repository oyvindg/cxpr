/**
 * @file basket.h
 * @brief Basket aggregate helpers built on top of cxpr.
 */

#ifndef CXPR_BASKET_H
#define CXPR_BASKET_H

#include <cxpr/ast.h>
#include <cxpr/context.h>
#include <cxpr/registry.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Return true when `name` is one of the basket builtins.
 * @param name Function name to inspect.
 * @return True for basket builtins such as `avg` and `count`.
 */
bool cxpr_basket_is_builtin(const char* name);

/**
 * @brief Return true when `name(argc)` is treated as a basket aggregate.
 * @param name Function name to inspect.
 * @param argc Argument count.
 * @return True when the call is a single-argument basket aggregate.
 */
bool cxpr_basket_is_aggregate_function(const char* name, size_t argc);

/**
 * @brief Register Dynasty basket builtins in a cxpr registry.
 * @param reg Destination registry.
 */
void cxpr_register_basket_builtins(cxpr_registry* reg);

/**
 * @brief Walk an AST and detect basket aggregate usage.
 * @param ast AST root node.
 * @return True when a basket aggregate call is present.
 */
bool cxpr_ast_uses_basket_aggregates(const cxpr_ast* ast);

/**
 * @brief Parse a source expression and detect basket aggregate usage.
 * @param source Expression source string.
 * @return True when the parsed expression uses basket aggregates.
 */
bool cxpr_expression_uses_basket_aggregates(const char* source);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_BASKET_H */
