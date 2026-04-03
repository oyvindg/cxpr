/**
 * @file context.c
 * @brief Context API and lookup caches for cxpr.
 */

#include "internal.h"
#include <stdio.h>
#include <stdint.h>

/**
 * @brief Return the direct-mapped cache bucket for one context key hash.
 *
 * @param cache Fixed-size cache array to index.
 * @param hash Precomputed string hash.
 * @return Cache bucket associated with the hash.
 */
static cxpr_context_entry_cache* cxpr_context_cache_bucket(cxpr_context_entry_cache* cache,
                                                           unsigned long hash) {
    return &cache[hash & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)];
}

/**
 * @brief Return the direct-mapped cache bucket for one caller key pointer.
 *
 * @param cache Fixed-size cache array to index.
 * @param key Caller-provided key pointer.
 * @return Cache bucket associated with the pointer identity.
 */
static cxpr_context_entry_cache* cxpr_context_pointer_cache_bucket(cxpr_context_entry_cache* cache,
                                                                   const char* key) {
    return &cache[((uintptr_t)key >> 4) & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)];
}

/**
 * @brief Resolve one mutable map entry through the caller-pointer cache.
 *
 * @param map Hash map to search.
 * @param cache Fixed-size pointer cache tied to the map.
 * @param key Lookup key pointer from the caller.
 * @return Mutable entry pointer, or NULL when the pointer cache misses.
 */
static cxpr_hashmap_entry* cxpr_context_lookup_pointer_cached_entry(cxpr_hashmap* map,
                                                                    cxpr_context_entry_cache* cache,
                                                                    const char* key) {
    cxpr_context_entry_cache* bucket;

    if (!map || !cache || !key) return NULL;

    bucket = cxpr_context_pointer_cache_bucket(cache, key);
    if (bucket->entries_base == map->entries && bucket->key_ref == key) {
        cxpr_hashmap_entry* e = &map->entries[bucket->slot];
        if (e->key && strcmp(e->key, key) == 0) return e;
    }

    return NULL;
}

/**
 * @brief Resolve one mutable map entry and refresh the corresponding context cache bucket.
 *
 * @param map Hash map to search.
 * @param cache Fixed-size cache array tied to the map.
 * @param key Lookup key.
 * @param hash Precomputed key hash.
 * @return Mutable entry pointer, or NULL when the key is absent.
 */
static cxpr_hashmap_entry* cxpr_context_lookup_cached_entry(cxpr_hashmap* map,
                                                            cxpr_context_entry_cache* cache,
                                                            const char* key,
                                                            unsigned long hash) {
    cxpr_context_entry_cache* bucket;
    cxpr_hashmap_entry* entry;

    if (!map || !cache || !key) return NULL;

    bucket = cxpr_context_cache_bucket(cache, hash);
    if (bucket->entries_base == map->entries && bucket->hash == hash &&
        bucket->key_ref && strcmp(bucket->key_ref, key) == 0) {
        return &map->entries[bucket->slot];
    }

    entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    if (entry) {
        bucket->key_ref = entry->key;
        bucket->hash = hash;
        bucket->slot = (size_t)(entry - map->entries);
        bucket->entries_base = map->entries;
        return entry;
    }

    bucket->key_ref = NULL;
    bucket->hash = 0;
    bucket->slot = 0;
    bucket->entries_base = NULL;
    return NULL;
}

/**
 * @brief Refresh one context cache bucket after inserting or relocating a key.
 *
 * @param map Hash map that owns the entry.
 * @param cache Fixed-size cache array tied to the map.
 * @param key Lookup key stored by the caller.
 * @param hash Precomputed key hash.
 */
static void cxpr_context_refresh_cache(cxpr_hashmap* map, cxpr_context_entry_cache* cache,
                                       const char* key, unsigned long hash) {
    cxpr_hashmap_entry* entry;
    cxpr_context_entry_cache* bucket;

    if (!map || !cache || !key) return;

    entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    bucket = cxpr_context_cache_bucket(cache, hash);
    bucket->key_ref = entry ? entry->key : NULL;
    bucket->hash = hash;
    bucket->slot = entry ? (size_t)(entry - map->entries) : 0;
    bucket->entries_base = entry ? map->entries : NULL;
}

/**
 * @brief Refresh one pointer-keyed context cache bucket after inserting or reusing a key.
 *
 * @param map Hash map that owns the entry.
 * @param cache Fixed-size pointer cache tied to the map.
 * @param key Caller-provided key pointer.
 * @param entry Mutable entry pointer to cache.
 */
static void cxpr_context_refresh_pointer_cache(cxpr_hashmap* map,
                                               cxpr_context_entry_cache* cache, const char* key,
                                               cxpr_hashmap_entry* entry) {
    cxpr_context_entry_cache* bucket;

    if (!map || !cache || !key || !entry) return;

    bucket = cxpr_context_pointer_cache_bucket(cache, key);
    bucket->key_ref = key;
    bucket->hash = 0;
    bucket->slot = (size_t)(entry - map->entries);
    bucket->entries_base = map->entries;
}

static void cxpr_context_clear_entry_cache(cxpr_context_entry_cache* cache) {
    if (!cache) return;
    memset(cache, 0, sizeof(cxpr_context_entry_cache) * CXPR_CONTEXT_ENTRY_CACHE_SIZE);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Context API
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_context* cxpr_context_new(void) {
    cxpr_context* ctx = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!ctx) return NULL;
    cxpr_hashmap_init(&ctx->variables);
    cxpr_hashmap_init(&ctx->params);
    cxpr_struct_map_init(&ctx->structs);
    cxpr_struct_map_init(&ctx->cached_structs);
    ctx->variables_version = 1;
    ctx->params_version = 1;
    ctx->parent = NULL;
    return ctx;
}

cxpr_context* cxpr_context_overlay_new(const cxpr_context* parent) {
    cxpr_context* ctx = cxpr_context_new();
    if (!ctx) return NULL;
    ctx->parent = parent;
    return ctx;
}

void cxpr_context_free(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_destroy(&ctx->variables);
    cxpr_hashmap_destroy(&ctx->params);
    cxpr_struct_map_destroy(&ctx->structs);
    cxpr_struct_map_destroy(&ctx->cached_structs);
    free(ctx);
}

cxpr_context* cxpr_context_clone(const cxpr_context* ctx) {
    cxpr_context* clone;
    cxpr_hashmap* var_clone;
    cxpr_hashmap* param_clone;

    if (!ctx) return NULL;

    clone = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!clone) return NULL;

    var_clone = cxpr_hashmap_clone(&ctx->variables);
    param_clone = cxpr_hashmap_clone(&ctx->params);
    if (!var_clone || !param_clone ||
        !cxpr_struct_map_clone(&clone->structs, &ctx->structs) ||
        !cxpr_struct_map_clone(&clone->cached_structs, &ctx->cached_structs)) {
        free(var_clone);
        free(param_clone);
        cxpr_struct_map_destroy(&clone->structs);
        cxpr_struct_map_destroy(&clone->cached_structs);
        free(clone);
        return NULL;
    }

    clone->variables = *var_clone;
    clone->params = *param_clone;
    clone->variables_version = ctx->variables_version;
    clone->params_version = ctx->params_version;
    clone->parent = NULL;
    free(var_clone);
    free(param_clone);
    return clone;
}

static void cxpr_context_set_hashed(cxpr_context* ctx, cxpr_hashmap* map,
                                    cxpr_context_entry_cache* cache,
                                    cxpr_context_entry_cache* ptr_cache,
                                    unsigned long* version, const char* name,
                                    unsigned long hash, double value) {
    cxpr_hashmap_entry* entry;

    if (!ctx || !map || !cache || !ptr_cache || !version || !name) return;

    entry = cxpr_context_lookup_pointer_cached_entry(map, ptr_cache, name);
    if (entry) {
        entry->value = value;
        return;
    }

    entry = cxpr_context_lookup_cached_entry(map, cache, name, hash);
    if (entry) {
        cxpr_context_refresh_pointer_cache(map, ptr_cache, name, entry);
        entry->value = value;
        return;
    }

    if (cxpr_hashmap_set_prehashed(map, name, hash, value)) {
        (*version)++;
    }

    cxpr_context_refresh_cache(map, cache, name, hash);
    entry = cxpr_hashmap_find_prehashed_slot(map, name, hash);
    cxpr_context_refresh_pointer_cache(map, ptr_cache, name, entry);
}

void cxpr_context_set_prehashed(cxpr_context* ctx, const char* name,
                                unsigned long hash, double value) {
    cxpr_context_set_hashed(ctx, &ctx->variables, ctx->variable_cache,
                            ctx->variable_ptr_cache, &ctx->variables_version,
                            name, hash, value);
}

void cxpr_context_set(cxpr_context* ctx, const char* name, double value) {
    if (!ctx || !name) return;
    cxpr_context_set_prehashed(ctx, name, cxpr_hash_string(name), value);
}

double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    entry = cxpr_context_lookup_pointer_cached_entry((cxpr_hashmap*)&ctx->variables,
                                                     ((cxpr_context*)ctx)->variable_ptr_cache,
                                                     name);
    if (entry) {
        if (found) *found = true;
        return entry->value;
    }
    hash = cxpr_hash_string(name);
    entry = cxpr_context_lookup_cached_entry((cxpr_hashmap*)&ctx->variables,
                                             ((cxpr_context*)ctx)->variable_cache, name, hash);
    if (entry) {
        cxpr_context_refresh_pointer_cache((cxpr_hashmap*)&ctx->variables,
                                           ((cxpr_context*)ctx)->variable_ptr_cache, name, entry);
        if (found) *found = true;
        return entry->value;
    }
    if (ctx->parent) return cxpr_context_get(ctx->parent, name, found);
    if (found) *found = false;
    return 0.0;
}

void cxpr_context_set_param_prehashed(cxpr_context* ctx, const char* name,
                                      unsigned long hash, double value) {
    cxpr_context_set_hashed(ctx, &ctx->params, ctx->param_cache,
                            ctx->param_ptr_cache, &ctx->params_version,
                            name, hash, value);
}

void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value) {
    if (!ctx || !name) return;
    cxpr_context_set_param_prehashed(ctx, name, cxpr_hash_string(name), value);
}

double cxpr_context_get_param(const cxpr_context* ctx, const char* name, bool* found) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    entry = cxpr_context_lookup_pointer_cached_entry((cxpr_hashmap*)&ctx->params,
                                                     ((cxpr_context*)ctx)->param_ptr_cache, name);
    if (entry) {
        if (found) *found = true;
        return entry->value;
    }
    hash = cxpr_hash_string(name);
    entry = cxpr_context_lookup_cached_entry((cxpr_hashmap*)&ctx->params,
                                             ((cxpr_context*)ctx)->param_cache, name, hash);
    if (entry) {
        cxpr_context_refresh_pointer_cache((cxpr_hashmap*)&ctx->params,
                                           ((cxpr_context*)ctx)->param_ptr_cache, name, entry);
        if (found) *found = true;
        return entry->value;
    }
    if (ctx->parent) return cxpr_context_get_param(ctx->parent, name, found);
    if (found) *found = false;
    return 0.0;
}

void cxpr_context_clear(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_clear(&ctx->variables);
    cxpr_hashmap_clear(&ctx->params);
    cxpr_struct_map_clear(&ctx->structs);
    cxpr_struct_map_clear(&ctx->cached_structs);
    cxpr_context_clear_entry_cache(ctx->variable_cache);
    cxpr_context_clear_entry_cache(ctx->param_cache);
    cxpr_context_clear_entry_cache(ctx->variable_ptr_cache);
    cxpr_context_clear_entry_cache(ctx->param_ptr_cache);
    ctx->variables_version++;
    ctx->params_version++;
}

void cxpr_context_clear_cached_structs(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_struct_map_clear(&ctx->cached_structs);
}

void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value) {
    if (!ctx) return;
    cxpr_context_store_struct(&ctx->structs, name, value);
}

void cxpr_context_set_cached_struct(cxpr_context* ctx, const char* name,
                                    const cxpr_struct_value* value) {
    if (!ctx) return;
    cxpr_context_store_struct(&ctx->cached_structs, name, value);
}

const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                 const char* name) {
    const cxpr_struct_value* value;

    if (!ctx) return NULL;
    value = cxpr_context_lookup_struct_map(&ctx->structs, name);
    if (value) return value;
    return ctx->parent ? cxpr_context_get_struct(ctx->parent, name) : NULL;
}

const cxpr_struct_value* cxpr_context_get_cached_struct(const cxpr_context* ctx,
                                                        const char* name) {
    const cxpr_struct_value* value;

    if (!ctx) return NULL;
    value = cxpr_context_lookup_struct_map(&ctx->cached_structs, name);
    if (value) return value;
    return ctx->parent ? cxpr_context_get_cached_struct(ctx->parent, name) : NULL;
}

cxpr_field_value cxpr_context_get_field(const cxpr_context* ctx, const char* name,
                                        const char* field, bool* found) {
    const cxpr_struct_value* s = cxpr_context_get_struct(ctx, name);

    if (!s) {
        if (found) *found = false;
        return cxpr_fv_double(0.0);
    }

    for (size_t i = 0; i < s->field_count; i++) {
        if (strcmp(s->field_names[i], field) == 0) {
            if (found) *found = true;
            return s->field_values[i];
        }
    }

    if (found) *found = false;
    return cxpr_fv_double(0.0);
}

void cxpr_context_set_fields(cxpr_context* ctx, const char* prefix,
                             const char* const* fields, const double* values,
                             size_t count) {
    char key[256];

    if (!ctx || !prefix || !fields || !values) return;

    for (size_t i = 0; i < count; i++) {
        if (!fields[i]) continue;
        snprintf(key, sizeof(key), "%s.%s", prefix, fields[i]);
        cxpr_context_set(ctx, key, values[i]);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Slot API
 * ═══════════════════════════════════════════════════════════════════════════ */

bool cxpr_context_slot_bind(cxpr_context* ctx, const char* name, cxpr_context_slot* slot) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!slot) return false;
    slot->_ptr = NULL;
    slot->_base = NULL;
    if (!ctx || !name) return false;

    hash = cxpr_hash_string(name);
    entry = cxpr_hashmap_find_prehashed_slot(&ctx->variables, name, hash);
    if (!entry) return false;

    slot->_ptr = &entry->value;
    slot->_base = ctx->variables.entries;
    return true;
}

bool cxpr_context_slot_valid(const cxpr_context* ctx, const cxpr_context_slot* slot) {
    return ctx && slot && slot->_ptr && slot->_base == (void*)ctx->variables.entries;
}

void cxpr_context_slot_set(cxpr_context_slot* slot, double value) {
    *slot->_ptr = value;
}

double cxpr_context_slot_get(const cxpr_context_slot* slot) {
    return *slot->_ptr;
}
