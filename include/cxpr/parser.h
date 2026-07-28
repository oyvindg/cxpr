/**
 * @file parser.h
 * @brief Public parser API for cxpr expressions.
 */

#ifndef CXPR_PARSER_H
#define CXPR_PARSER_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a parser instance.
 * @return Newly allocated parser, or NULL on allocation failure.
 */
cxpr_expr_parser* cxpr_expr_parser_new(void);
/**
 * @brief Free a parser instance.
 * @param p Parser to free. May be NULL.
 */
void cxpr_expr_parser_free(cxpr_expr_parser* p);
/**
 * @brief Parse an expression string into an AST.
 * @param p Parser instance to use.
 * @param expression NUL-terminated expression source.
 * @param err Optional error output.
 * @return Newly allocated AST on success, or NULL on parse failure.
 */
cxpr_expr_ast* cxpr_expr_ast_parse(cxpr_expr_parser* p, const char* expression, cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PARSER_H */
