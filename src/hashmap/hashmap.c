/**
 * @file context_hashmap.c
 * @brief Internal string-double hashmap support for cxpr contexts.
 */

#include "internal.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Hash map implementation
 * ═══════════════════════════════════════════════════════════════════════════ */

unsigned long cxpr_hash_string(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

void cxpr_hashmap_init(cxpr_hashmap* map) {
    map->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->entries = (cxpr_hashmap_entry*)calloc(map->capacity, sizeof(cxpr_hashmap_entry));
}

void cxpr_hashmap_destroy(cxpr_hashmap* map) {
    size_t i;

    if (!map->entries) return;
    for (i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

static bool cxpr_hashmap_grow(cxpr_hashmap* map) {
    size_t i;
    size_t new_capacity;
    cxpr_hashmap_entry* new_entries;

    if (map->capacity > SIZE_MAX / 2) return false;
    new_capacity = map->capacity * 2;
    new_entries = (cxpr_hashmap_entry*)calloc(new_capacity, sizeof(cxpr_hashmap_entry));
    if (!new_entries) return false;

    for (i = 0; i < map->capacity; i++) {
        unsigned long hash;

        if (!map->entries[i].key) continue;
        hash = cxpr_hash_string(map->entries[i].key) % new_capacity;
        while (new_entries[hash].key) {
            hash = (hash + 1) % new_capacity;
        }
        new_entries[hash] = map->entries[i];
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return true;
}

/**
 * @brief Find the mutable backing slot for a prehashed key, or NULL if absent.
 *
 * @param map Hash map to probe.
 * @param key Lookup key.
 * @param hash Precomputed hash for the key.
 * @return Mutable entry pointer, or NULL when the key is missing.
 */
cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_slot(cxpr_hashmap* map, const char* key,
                                                     unsigned long hash) {
    if (!map || !map->entries || map->count == 0) return NULL;

    hash %= map->capacity;
    while (map->entries[hash].key) {
        if (strcmp(map->entries[hash].key, key) == 0) {
            return &map->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }

    return NULL;
}

bool cxpr_hashmap_set(cxpr_hashmap* map, const char* key, double value) {
    cxpr_hashmap_entry* entry;
    unsigned long hash;

    if (!map || !key) return false;

    hash = cxpr_hash_string(key);
    entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    if (entry) {
        entry->value = value;
        return false;
    }

    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR) {
        cxpr_hashmap_grow(map);
    }
    hash %= map->capacity;

    while (map->entries[hash].key) {
        hash = (hash + 1) % map->capacity;
    }

    map->entries[hash].key = cxpr_strdup(key);
    map->entries[hash].value = value;
    map->count++;
    return true;
}

bool cxpr_hashmap_set_prehashed(cxpr_hashmap* map, const char* key,
                                unsigned long hash, double value) {
    cxpr_hashmap_entry* entry;
    unsigned long slot;

    if (!map || !key) return false;

    entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    if (entry) {
        entry->value = value;
        return false;
    }

    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR) {
        cxpr_hashmap_grow(map);
    }

    slot = hash % map->capacity;
    while (map->entries[slot].key) {
        slot = (slot + 1) % map->capacity;
    }

    map->entries[slot].key = cxpr_strdup(key);
    map->entries[slot].value = value;
    map->count++;
    return true;
}

double cxpr_hashmap_get_prehashed(const cxpr_hashmap* map, const char* key,
                                  unsigned long hash, bool* found) {
    if (!map->entries || map->count == 0) {
        if (found) *found = false;
        return 0.0;
    }

    hash %= map->capacity;
    while (map->entries[hash].key) {
        if (strcmp(map->entries[hash].key, key) == 0) {
            if (found) *found = true;
            return map->entries[hash].value;
        }
        hash = (hash + 1) % map->capacity;
    }

    if (found) *found = false;
    return 0.0;
}

const cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_entry(const cxpr_hashmap* map,
                                                            const char* key,
                                                            unsigned long hash) {
    if (!map || !map->entries || map->count == 0) return NULL;

    hash %= map->capacity;
    while (map->entries[hash].key) {
        if (strcmp(map->entries[hash].key, key) == 0) {
            return &map->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }

    return NULL;
}

double cxpr_hashmap_get(const cxpr_hashmap* map, const char* key, bool* found) {
    return cxpr_hashmap_get_prehashed(map, key, cxpr_hash_string(key), found);
}

void cxpr_hashmap_clear(cxpr_hashmap* map) {
    size_t i;

    for (i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
        map->entries[i].key = NULL;
        map->entries[i].value = 0.0;
    }
    map->count = 0;
}

cxpr_hashmap* cxpr_hashmap_clone(const cxpr_hashmap* map) {
    cxpr_hashmap* clone = (cxpr_hashmap*)malloc(sizeof(cxpr_hashmap));
    size_t i;

    if (!clone) return NULL;

    clone->capacity = map->capacity;
    clone->count = map->count;
    clone->entries = (cxpr_hashmap_entry*)calloc(clone->capacity, sizeof(cxpr_hashmap_entry));
    if (!clone->entries) {
        free(clone);
        return NULL;
    }

    for (i = 0; i < map->capacity; i++) {
        if (!map->entries[i].key) continue;
        clone->entries[i].key = cxpr_strdup(map->entries[i].key);
        clone->entries[i].value = map->entries[i].value;
    }
    return clone;
}
