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

/** @brief One owned named array binding stored in an internal array map. */
typedef struct {
    char* name;
    cxpr_array_value* value;
} cxpr_array_map_entry;

/** @brief Internal dynamic array of named array bindings. */
typedef struct {
    cxpr_array_map_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_array_map;

/** @brief One owned named bool binding stored in an internal bool map. */
typedef struct {
    char* name;
    bool value;
} cxpr_bool_map_entry;

/** @brief Internal dynamic array of named bool bindings. */
typedef struct {
    cxpr_bool_map_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_bool_map;

/** @brief One owned named string binding stored in an internal string map. */
typedef struct {
    char* name;
    char* value;
} cxpr_string_map_entry;

/** @brief Internal dynamic array of named string bindings. */
typedef struct {
    cxpr_string_map_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_string_map;

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
/** @brief Remove one struct binding by name from a struct map. */
void cxpr_context_remove_struct(cxpr_struct_map* map, const char* name);
/** @brief Look up one struct binding by name from a struct map. */
const cxpr_struct_value* cxpr_context_lookup_struct_map(const cxpr_struct_map* map,
                                                        const char* name);
/** @brief Initialize one empty internal array map. */
void cxpr_array_map_init(cxpr_array_map* map);
/** @brief Free all storage owned by one internal array map. */
void cxpr_array_map_destroy(cxpr_array_map* map);
/** @brief Remove all bindings from one internal array map while keeping capacity. */
void cxpr_array_map_clear(cxpr_array_map* map);
/** @brief Deep-clone one internal array map. */
bool cxpr_array_map_clone(cxpr_array_map* dst, const cxpr_array_map* src);
/** @brief Store or replace one deep-copied array binding in an array map. */
void cxpr_context_store_array(cxpr_array_map* map, const char* name,
                              const cxpr_array_value* value);
/** @brief Remove one array binding by name from an array map. */
void cxpr_context_remove_array(cxpr_array_map* map, const char* name);
/** @brief Look up one array binding by name from an array map. */
const cxpr_array_value* cxpr_context_lookup_array_map(const cxpr_array_map* map,
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
    cxpr_bool_map bools;
    cxpr_bool_map bool_params;
    cxpr_string_map strings;
    cxpr_string_map string_params;
    cxpr_struct_map structs;
    cxpr_struct_map cached_structs;
    cxpr_array_map arrays;
    cxpr_array_map array_params;
    cxpr_eval_memo eval_memo;
    cxpr_context_entry_cache variable_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache variable_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    unsigned long variables_version;
    unsigned long params_version;
    const struct cxpr_context* parent;
    const struct cxpr_evaluator* expression_scope;
    struct cxpr_context* overlay_cache_next;
};

#endif /* CXPR_CONTEXT_STATE_H */
