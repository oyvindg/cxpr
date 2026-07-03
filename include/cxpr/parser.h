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
cxpr_parser* cxpr_parser_new(void);
/**
 * @brief Free a parser instance.
 * @param p Parser to free. May be NULL.
 */
void cxpr_parser_free(cxpr_parser* p);
/**
 * @brief Parse an expression string into an AST.
 * @param p Parser instance to use.
 * @param expression NUL-terminated expression source.
 * @param err Optional error output.
 * @return Newly allocated AST on success, or NULL on parse failure.
 */
cxpr_ast* cxpr_parse(cxpr_parser* p, const char* expression, cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_PARSER_H */
