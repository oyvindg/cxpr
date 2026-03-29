/**
 * @file context.c
 * @brief Variable and parameter bindings for cxpr.
 *
 * Implements the context API using internal hash maps for both
 * runtime variables and compile-time parameters ($name).
 */

#include "internal.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Hash map implementation
 * ═══════════════════════════════════════════════════════════════════════════ */

/** @brief djb2 hash function for string keys. */
static unsigned long cxpr_hash_string(const char* str) {
    /* djb2 hash */
    unsigned long hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

/**
 * @brief Initialize a hash map with default capacity.
 * @param[in] map Hash map to initialize
 */
void cxpr_hashmap_init(cxpr_hashmap* map) {
    map->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    map->count = 0;
    map->entries = (cxpr_hashmap_entry*)calloc(map->capacity, sizeof(cxpr_hashmap_entry));
}

/**
 * @brief Free all entries and the map's storage.
 * @param[in] map Hash map to destroy
 */
void cxpr_hashmap_destroy(cxpr_hashmap* map) {
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

/** @brief Double the hash map's capacity and rehash entries. */
static void cxpr_hashmap_grow(cxpr_hashmap* map) {
    size_t new_capacity = map->capacity * 2;
    cxpr_hashmap_entry* new_entries = (cxpr_hashmap_entry*)calloc(new_capacity, sizeof(cxpr_hashmap_entry));
    if (!new_entries) return;

    for (size_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key) {
            unsigned long hash = cxpr_hash_string(map->entries[i].key) % new_capacity;
            while (new_entries[hash].key) {
                hash = (hash + 1) % new_capacity;
            }
            new_entries[hash] = map->entries[i];
        }
    }
    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

/**
 * @brief Insert or update a key-value pair.
 * @param[in] map Hash map
 * @param[in] key Key string
 * @param[in] value Value to store
 */
void cxpr_hashmap_set(cxpr_hashmap* map, const char* key, double value) {
    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR) {
        cxpr_hashmap_grow(map);
    }
    unsigned long hash = cxpr_hash_string(key) % map->capacity;
    while (map->entries[hash].key) {
        if (strcmp(map->entries[hash].key, key) == 0) {
            map->entries[hash].value = value;
            return;
        }
        hash = (hash + 1) % map->capacity;
    }
    map->entries[hash].key = strdup(key);
    map->entries[hash].value = value;
    map->count++;
}

/**
 * @brief Look up a value by key.
 * @param[in] map Hash map
 * @param[in] key Key string
 * @param[out] found Optional; set to true if key was found
 * @return The value, or 0.0 if not found
 */
double cxpr_hashmap_get(const cxpr_hashmap* map, const char* key, bool* found) {
    if (!map->entries || map->count == 0) {
        if (found) *found = false;
        return 0.0;
    }
    unsigned long hash = cxpr_hash_string(key) % map->capacity;
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

/**
 * @brief Remove all entries without freeing the map itself.
 * @param[in] map Hash map to clear
 */
void cxpr_hashmap_clear(cxpr_hashmap* map) {
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
        map->entries[i].key = NULL;
        map->entries[i].value = 0.0;
    }
    map->count = 0;
}

/**
 * @brief Deep copy a hash map.
 * @param[in] map Hash map to clone
 * @return New hash map, or NULL on failure
 */
cxpr_hashmap* cxpr_hashmap_clone(const cxpr_hashmap* map) {
    cxpr_hashmap* clone = (cxpr_hashmap*)malloc(sizeof(cxpr_hashmap));
    if (!clone) return NULL;
    clone->capacity = map->capacity;
    clone->count = map->count;
    clone->entries = (cxpr_hashmap_entry*)calloc(clone->capacity, sizeof(cxpr_hashmap_entry));
    if (!clone->entries) { free(clone); return NULL; }
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->entries[i].key) {
            clone->entries[i].key = strdup(map->entries[i].key);
            clone->entries[i].value = map->entries[i].value;
        }
    }
    return clone;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Context API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new empty context.
 * @return New context, or NULL on allocation failure
 */
cxpr_context* cxpr_context_new(void) {
    cxpr_context* ctx = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!ctx) return NULL;
    cxpr_hashmap_init(&ctx->variables);
    cxpr_hashmap_init(&ctx->params);
    return ctx;
}

/**
 * @brief Free a context and its internal hash maps.
 * @param[in] ctx Context to free
 */
void cxpr_context_free(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_destroy(&ctx->variables);
    cxpr_hashmap_destroy(&ctx->params);
    free(ctx);
}

/**
 * @brief Deep copy a context (variables and params).
 * @param[in] ctx Context to clone
 * @return New context, or NULL on failure
 */
cxpr_context* cxpr_context_clone(const cxpr_context* ctx) {
    if (!ctx) return NULL;
    cxpr_context* clone = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!clone) return NULL;

    cxpr_hashmap* var_clone = cxpr_hashmap_clone(&ctx->variables);
    cxpr_hashmap* param_clone = cxpr_hashmap_clone(&ctx->params);
    if (!var_clone || !param_clone) {
        free(var_clone);
        free(param_clone);
        free(clone);
        return NULL;
    }
    clone->variables = *var_clone;
    clone->params = *param_clone;
    free(var_clone);
    free(param_clone);
    return clone;
}

/**
 * @brief Set a runtime variable value.
 * @param[in] ctx Context
 * @param[in] name Variable name
 * @param[in] value Value to set
 */
void cxpr_context_set(cxpr_context* ctx, const char* name, double value) {
    if (ctx) cxpr_hashmap_set(&ctx->variables, name, value);
}

/**
 * @brief Get a runtime variable value.
 * @param[in] ctx Context
 * @param[in] name Variable name
 * @param[out] found Optional; set to true if variable exists
 * @return The value, or 0.0 if not found
 */
double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx) { if (found) *found = false; return 0.0; }
    return cxpr_hashmap_get(&ctx->variables, name, found);
}

/**
 * @brief Set a compile-time parameter ($variable) value.
 * @param[in] ctx Context
 * @param[in] name Parameter name
 * @param[in] value Value to set
 */
void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value) {
    if (ctx) cxpr_hashmap_set(&ctx->params, name, value);
}

/**
 * @brief Get a compile-time parameter value.
 * @param[in] ctx Context
 * @param[in] name Parameter name
 * @param[out] found Optional; set to true if parameter exists
 * @return The value, or 0.0 if not found
 */
double cxpr_context_get_param(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx) { if (found) *found = false; return 0.0; }
    return cxpr_hashmap_get(&ctx->params, name, found);
}

/**
 * @brief Clear all variables and parameters.
 * @param[in] ctx Context to clear
 */
void cxpr_context_clear(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_clear(&ctx->variables);
    cxpr_hashmap_clear(&ctx->params);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Prefixed Field API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Set multiple fields with a common prefix.
 *
 * Each field is stored as "prefix.field" in the context.
 *
 * @param[in] ctx Context
 * @param[in] prefix Common prefix (e.g., "macd")
 * @param[in] fields Array of field names
 * @param[in] values Array of field values (parallel to fields)
 * @param[in] count Number of fields
 */
void cxpr_context_set_fields(cxpr_context* ctx, const char* prefix,
                             const char* const* fields, const double* values,
                             size_t count) {
    if (!ctx || !prefix || !fields || !values) return;

    char key[256];
    for (size_t i = 0; i < count; i++) {
        if (!fields[i]) continue;
        snprintf(key, sizeof(key), "%s.%s", prefix, fields[i]);
        cxpr_hashmap_set(&ctx->variables, key, values[i]);
    }
}
