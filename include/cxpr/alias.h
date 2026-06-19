/**
 * @file alias.h
 * @brief AST-level expression alias expansion.
 */

#ifndef CXPR_ALIAS_H
#define CXPR_ALIAS_H

#include <cxpr/types.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    const char* expression;
} cxpr_alias;

/**
 * @brief Expand named expression aliases in expression source.
 *
 * Alias expansion happens on parsed ASTs, not with textual replacement. The
 * returned source can be parsed again as regular cxpr expression source.
 *
 * @param expression Expression source to expand.
 * @param aliases Alias table.
 * @param alias_count Number of alias table entries.
 * @param out_expression Owned expanded expression string on success.
 * @param err Optional parse/expansion error output.
 * @return 1 on success, 0 on parse/allocation/cycle failure.
 */
int cxpr_expand_aliases(const char* expression,
                        const cxpr_alias* aliases,
                        size_t alias_count,
                        char** out_expression,
                        cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_ALIAS_H */
