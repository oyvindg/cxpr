/**
 * @file internal.h
 * @brief Internal string-double hashmap support for cxpr.
 */

#ifndef CXPR_HASHMAP_INTERNAL_H
#define CXPR_HASHMAP_INTERNAL_H

#include "../core.h" // IWYU pragma: keep

/** @brief Initial open-addressed slot count for internal cxpr hash maps. */
#define CXPR_HASHMAP_INITIAL_CAPACITY 32
/** @brief Maximum load factor before the internal cxpr hash map grows. */
#define CXPR_HASHMAP_LOAD_FACTOR 0.75

/** @brief One owned string-to-double entry stored in the internal hash map. */
typedef struct {
    /** @brief Owned NUL-terminated key string. */
    char* key;
    /** @brief Numeric payload associated with `key`. */
    double value;
} cxpr_hashmap_entry;

/** @brief Internal open-addressed hashmap used by cxpr contexts. */
typedef struct {
    /** @brief Dense slot array storing keys and values. */
    cxpr_hashmap_entry* entries;
    /** @brief Allocated slot count in `entries`. */
    size_t capacity;
    /** @brief Number of occupied key slots. */
    size_t count;
} cxpr_hashmap;

/**
 * @brief Look up one immutable entry using a precomputed hash.
 * @param map Map to probe.
 * @param key Lookup key.
 * @param hash Precomputed hash for `key`.
 * @return Matching entry, or NULL when absent.
 */
const cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_entry(const cxpr_hashmap* map,
                                                            const char* key,
                                                            unsigned long hash);
/**
 * @brief Look up one mutable entry using a precomputed hash.
 * @param map Map to probe.
 * @param key Lookup key.
 * @param hash Precomputed hash for `key`.
 * @return Matching entry, or NULL when absent.
 */
cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_slot(cxpr_hashmap* map, const char* key,
                                                     unsigned long hash);
/**
 * @brief Initialize one empty internal hash map.
 * @param map Map storage to initialize.
 */
void cxpr_hashmap_init(cxpr_hashmap* map);
/**
 * @brief Release all storage owned by one internal hash map.
 * @param map Map to destroy.
 */
void cxpr_hashmap_destroy(cxpr_hashmap* map);
/**
 * @brief Insert or update one key using on-demand hashing.
 * @param map Map to modify.
 * @param key Key string to insert or update.
 * @param value Numeric payload to store.
 * @return True when a new key was inserted, false when an existing key was updated.
 */
bool cxpr_hashmap_set(cxpr_hashmap* map, const char* key, double value);
/**
 * @brief Insert or update one key using a caller-provided hash.
 * @param map Map to modify.
 * @param key Key string to insert or update.
 * @param hash Precomputed hash for `key`.
 * @param value Numeric payload to store.
 * @return True when a new key was inserted, false when an existing key was updated.
 */
bool cxpr_hashmap_set_prehashed(cxpr_hashmap* map, const char* key,
                                unsigned long hash, double value);
/**
 * @brief Look up one key using on-demand hashing.
 * @param map Map to query.
 * @param key Lookup key.
 * @param found Optional success flag output.
 * @return Stored value, or `0.0` on miss.
 */
double cxpr_hashmap_get(const cxpr_hashmap* map, const char* key, bool* found);
/**
 * @brief Compute the internal hash used by cxpr string maps.
 * @param str NUL-terminated key string.
 * @return Hash value for `str`.
 */
unsigned long cxpr_hash_string(const char* str);
/**
 * @brief Look up one key using a caller-provided hash.
 * @param map Map to query.
 * @param key Lookup key.
 * @param hash Precomputed hash for `key`.
 * @param found Optional success flag output.
 * @return Stored value, or `0.0` on miss.
 */
double cxpr_hashmap_get_prehashed(const cxpr_hashmap* map, const char* key,
                                  unsigned long hash, bool* found);
/**
 * @brief Remove all entries while keeping allocated capacity.
 * @param map Map to clear.
 */
void cxpr_hashmap_clear(cxpr_hashmap* map);
/**
 * @brief Deep-clone one internal hash map.
 * @param map Source map to copy.
 * @return Newly allocated clone, or NULL on allocation failure.
 */
cxpr_hashmap* cxpr_hashmap_clone(const cxpr_hashmap* map);

#endif /* CXPR_HASHMAP_INTERNAL_H */
