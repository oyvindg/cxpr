/**
 * @file state.h
 * @brief Internal context storage layout shared across cxpr modules.
 */

#ifndef CXPR_CONTEXT_STATE_H
#define CXPR_CONTEXT_STATE_H

#include "hashmap/internal.h"

/** @brief Fixed-size direct-mapped cache used for repeated context entry lookups. */
#define CXPR_CONTEXT_ENTRY_CACHE_SIZE 64

/** @brief One owned named struct binding stored in an internal struct map. */
typedef struct {
    char* name;
    cxpr_struct_value* value;
} cxpr_struct_map_entry;

/** @brief Internal dynamic array of named struct bindings. */
typedef struct {
    cxpr_struct_map_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_struct_map;

/** @brief Initialize one empty internal struct map. */
void cxpr_struct_map_init(cxpr_struct_map* map);
/** @brief Free all storage owned by one internal struct map. */
void cxpr_struct_map_destroy(cxpr_struct_map* map);
/** @brief Remove all bindings from one internal struct map while keeping capacity. */
void cxpr_struct_map_clear(cxpr_struct_map* map);
/** @brief Deep-clone one internal struct map. */
bool cxpr_struct_map_clone(cxpr_struct_map* dst, const cxpr_struct_map* src);
/** @brief Store or replace one deep-copied struct binding in a struct map. */
void cxpr_context_store_struct(cxpr_struct_map* map, const char* name,
                               const cxpr_struct_value* value);
/** @brief Look up one struct binding by name from a struct map. */
const cxpr_struct_value* cxpr_context_lookup_struct_map(const cxpr_struct_map* map,
                                                        const char* name);
/** @brief Store one cached producer struct result on a context. */
void cxpr_context_set_cached_struct(cxpr_context* ctx, const char* name,
                                    const cxpr_struct_value* value);
/** @brief Look up one cached producer struct result from a context. */
const cxpr_struct_value* cxpr_context_get_cached_struct(const cxpr_context* ctx,
                                                        const char* name);
/** @brief Attach evaluator expression results as a temporary lookup scope. */
void cxpr_context_set_expression_scope(cxpr_context* ctx,
                                       const struct cxpr_evaluator* evaluator);
/** @brief Remove any active evaluator expression scope from a context. */
void cxpr_context_clear_expression_scope(cxpr_context* ctx);

/** @brief One cache entry for hashed or pointer-stable context lookups. */
typedef struct {
    const char* key_ref;
    unsigned long hash;
    size_t slot;
    cxpr_hashmap_entry* entries_base;
} cxpr_context_entry_cache;

/** @brief One per-evaluation memoized AST result. */
typedef struct {
    const struct cxpr_ast* ast;
    unsigned long hash;
    cxpr_value value;
} cxpr_eval_memo_entry;

/** @brief Dynamic per-context memo table for structurally equal AST subtrees. */
typedef struct {
    cxpr_eval_memo_entry* entries;
    size_t capacity;
    size_t count;
    size_t depth;
} cxpr_eval_memo;

/** @brief Internal owned context storage backing the public `cxpr_context` handle. */
struct cxpr_context {
    cxpr_hashmap variables;
    cxpr_hashmap params;
    cxpr_struct_map structs;
    cxpr_struct_map cached_structs;
    cxpr_eval_memo eval_memo;
    cxpr_context_entry_cache variable_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache variable_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    unsigned long variables_version;
    unsigned long params_version;
    const struct cxpr_context* parent;
    const struct cxpr_evaluator* expression_scope;
};

#endif /* CXPR_CONTEXT_STATE_H */
