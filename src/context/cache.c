/**
 * @file cache.c
 * @brief Context lookup-cache helpers.
 */

#include "internal.h"

#include <stdint.h>

static cxpr_context_entry_cache* cxpr_context_cache_bucket(cxpr_context_entry_cache* cache,
                                                           unsigned long hash) {
    return &cache[hash & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)];
}

static cxpr_context_entry_cache* cxpr_context_pointer_cache_bucket(cxpr_context_entry_cache* cache,
                                                                   const char* key) {
    return &cache[((uintptr_t)key >> 4) & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)];
}

cxpr_hashmap_entry* cxpr_context_lookup_pointer_cached_entry(cxpr_hashmap* map,
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

cxpr_hashmap_entry* cxpr_context_lookup_cached_entry(cxpr_hashmap* map,
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

void cxpr_context_refresh_cache(cxpr_hashmap* map, cxpr_context_entry_cache* cache,
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

void cxpr_context_refresh_pointer_cache(cxpr_hashmap* map,
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

void cxpr_context_clear_entry_cache(cxpr_context_entry_cache* cache) {
    if (!cache) return;
    memset(cache, 0, sizeof(cxpr_context_entry_cache) * CXPR_CONTEXT_ENTRY_CACHE_SIZE);
}
