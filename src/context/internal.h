/**
 * @file internal.h
 * @brief Internal context helpers shared across cxpr modules.
 */

#ifndef CXPR_CONTEXT_INTERNAL_H
#define CXPR_CONTEXT_INTERNAL_H

#include "state.h"

/** @brief Look up a mutable entry using the pointer-based context cache. */
cxpr_hashmap_entry* cxpr_context_lookup_pointer_cached_entry(cxpr_hashmap* map,
                                                             cxpr_context_entry_cache* cache,
                                                             const char* key);
/** @brief Look up a mutable entry using the hash-based context cache. */
cxpr_hashmap_entry* cxpr_context_lookup_cached_entry(cxpr_hashmap* map,
                                                     cxpr_context_entry_cache* cache,
                                                     const char* key,
                                                     unsigned long hash);
/** @brief Refresh one hash-based context cache entry after a successful lookup. */
void cxpr_context_refresh_cache(cxpr_hashmap* map, cxpr_context_entry_cache* cache,
                                const char* key, unsigned long hash);
/** @brief Refresh one pointer-based context cache entry after a successful lookup. */
void cxpr_context_refresh_pointer_cache(cxpr_hashmap* map,
                                        cxpr_context_entry_cache* cache, const char* key,
                                        cxpr_hashmap_entry* entry);
/** @brief Clear one direct-mapped context cache array. */
void cxpr_context_clear_entry_cache(cxpr_context_entry_cache* cache);

/** @brief Find the mutable storage slot for one variable binding. */
static inline cxpr_hashmap_entry* cxpr_context_find_variable_slot(cxpr_context* ctx,
                                                                  const char* key,
                                                                  unsigned long hash) {
    return cxpr_hashmap_find_prehashed_slot(&ctx->variables, key, hash);
}

/** @brief Return the current base pointer of the variable map entry array. */
static inline void* cxpr_context_variables_base(const cxpr_context* ctx) {
    return ctx ? (void*)ctx->variables.entries : NULL;
}

#endif /* CXPR_CONTEXT_INTERNAL_H */
