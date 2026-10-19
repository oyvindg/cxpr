/**
 * @file doc_tooling.h
 * @brief JSON document analysis used by editors and language servers.
 */
#ifndef CXPR_DOC_TOOLING_H
#define CXPR_DOC_TOOLING_H

#include <cxpr/builtin.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Return an allocated JSON analysis document, or NULL on allocation failure. */
char* cxpr_document_analyze_json(const char* source, const char* source_name);

/** Resolve the symbol at a zero-based line/column as editor-neutral JSON. */
char* cxpr_document_resolve_json(const char* source,
                                 const char* source_name,
                                 size_t line,
                                 size_t column);

/** Resolve a symbol while merging host-owned function metadata with cxpr builtins. */
char* cxpr_document_resolve_with_builtins_json(
    const char* source,
    const char* source_name,
    size_t line,
    size_t column,
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count);

/** Return editor-neutral completion candidates at a zero-based line/column. */
char* cxpr_document_complete_json(const char* source,
                                  const char* source_name,
                                  size_t line,
                                  size_t column);

/** Complete while merging host-owned function metadata with cxpr builtins. */
char* cxpr_document_complete_with_builtins_json(
    const char* source,
    const char* source_name,
    size_t line,
    size_t column,
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count);

/** Return the complete default function registry as editor-neutral JSON. */
char* cxpr_document_query_functions_json(void);

/** Return cxpr and host-owned function metadata as one editor-neutral JSON array. */
char* cxpr_document_query_functions_with_builtins_json(
    const cxpr_function_info* host_builtins,
    size_t host_builtin_count);

/** Release JSON returned by any cxpr_document_*_json function. */
void cxpr_document_json_free(char* json);

#ifdef __cplusplus
}
#endif

#endif
