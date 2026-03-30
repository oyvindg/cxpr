/**
 * @file context.c
 * @brief Variable, parameter, and native struct bindings for cxpr.
 */

#include "internal.h"
#include <stdio.h>

static cxpr_field_value cxpr_field_value_clone(const cxpr_field_value* value);

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
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

static bool cxpr_hashmap_grow(cxpr_hashmap* map) {
    size_t new_capacity = map->capacity * 2;
    cxpr_hashmap_entry* new_entries =
        (cxpr_hashmap_entry*)calloc(new_capacity, sizeof(cxpr_hashmap_entry));
    if (!new_entries) return false;

    for (size_t i = 0; i < map->capacity; i++) {
        if (!map->entries[i].key) continue;
        unsigned long hash = cxpr_hash_string(map->entries[i].key) % new_capacity;
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
static cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_slot(cxpr_hashmap* map, const char* key,
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
        hash %= map->capacity;
    } else {
        hash %= map->capacity;
    }

    while (map->entries[hash].key) {
        hash = (hash + 1) % map->capacity;
    }

    map->entries[hash].key = strdup(key);
    map->entries[hash].value = value;
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
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].key);
        map->entries[i].key = NULL;
        map->entries[i].value = 0.0;
    }
    map->count = 0;
}

cxpr_hashmap* cxpr_hashmap_clone(const cxpr_hashmap* map) {
    cxpr_hashmap* clone = (cxpr_hashmap*)malloc(sizeof(cxpr_hashmap));
    if (!clone) return NULL;

    clone->capacity = map->capacity;
    clone->count = map->count;
    clone->entries = (cxpr_hashmap_entry*)calloc(clone->capacity, sizeof(cxpr_hashmap_entry));
    if (!clone->entries) {
        free(clone);
        return NULL;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        if (!map->entries[i].key) continue;
        clone->entries[i].key = strdup(map->entries[i].key);
        clone->entries[i].value = map->entries[i].value;
    }
    return clone;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Typed struct values
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cxpr_struct_value_reset(cxpr_struct_value* s) {
    if (!s) return;
    for (size_t i = 0; i < s->field_count; i++) {
        free((char*)s->field_names[i]);
        if (s->field_values[i].type == CXPR_FIELD_STRUCT) {
            cxpr_struct_value_free(s->field_values[i].s);
        }
    }
    free(s->field_names);
    free(s->field_values);
    s->field_names = NULL;
    s->field_values = NULL;
    s->field_count = 0;
}

static cxpr_field_value cxpr_field_value_clone(const cxpr_field_value* value) {
    if (!value) return cxpr_fv_double(0.0);

    switch (value->type) {
    case CXPR_FIELD_DOUBLE:
        return cxpr_fv_double(value->d);
    case CXPR_FIELD_BOOL:
        return cxpr_fv_bool(value->b);
    case CXPR_FIELD_STRUCT:
        return cxpr_fv_struct(cxpr_struct_value_new(
            value->s ? (const char* const*)value->s->field_names : NULL,
            value->s ? value->s->field_values : NULL,
            value->s ? value->s->field_count : 0));
    default:
        return cxpr_fv_double(0.0);
    }
}

cxpr_struct_value* cxpr_struct_value_new(const char* const* field_names,
                                         const cxpr_field_value* field_values,
                                         size_t field_count) {
    cxpr_struct_value* s = (cxpr_struct_value*)calloc(1, sizeof(cxpr_struct_value));
    if (!s) return NULL;

    s->field_count = field_count;
    if (field_count == 0) return s;

    s->field_names = (const char**)calloc(field_count, sizeof(char*));
    s->field_values = (cxpr_field_value*)calloc(field_count, sizeof(cxpr_field_value));
    if (!s->field_names || !s->field_values) {
        cxpr_struct_value_free(s);
        return NULL;
    }

    for (size_t i = 0; i < field_count; i++) {
        s->field_names[i] = strdup(field_names[i]);
        if (!s->field_names[i]) {
            cxpr_struct_value_free(s);
            return NULL;
        }
        s->field_values[i] = cxpr_field_value_clone(&field_values[i]);
        if (field_values[i].type == CXPR_FIELD_STRUCT && field_values[i].s &&
            !s->field_values[i].s) {
            cxpr_struct_value_free(s);
            return NULL;
        }
    }

    return s;
}

void cxpr_struct_value_free(cxpr_struct_value* s) {
    if (!s) return;
    cxpr_struct_value_reset(s);
    free(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Struct map
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_struct_map_entry* cxpr_struct_map_find_slot(const cxpr_struct_map* map,
                                                        const char* name) {
    unsigned long hash;

    if (!map->entries || map->capacity == 0) return NULL;

    hash = cxpr_hash_string(name) % map->capacity;
    while (map->entries[hash].name) {
        if (strcmp(map->entries[hash].name, name) == 0) {
            return &((cxpr_struct_map*)map)->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }
    return &((cxpr_struct_map*)map)->entries[hash];
}

static void cxpr_struct_map_grow(cxpr_struct_map* map) {
    size_t new_capacity = map->capacity * 2;
    cxpr_struct_map_entry* new_entries =
        (cxpr_struct_map_entry*)calloc(new_capacity, sizeof(cxpr_struct_map_entry));
    if (!new_entries) return;

    for (size_t i = 0; i < map->capacity; i++) {
        if (!map->entries[i].name) continue;
        unsigned long hash = cxpr_hash_string(map->entries[i].name) % new_capacity;
        while (new_entries[hash].name) {
            hash = (hash + 1) % new_capacity;
        }
        new_entries[hash] = map->entries[i];
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
}

void cxpr_struct_map_init(cxpr_struct_map* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}

void cxpr_struct_map_destroy(cxpr_struct_map* map) {
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        cxpr_struct_value_free(map->entries[i].value);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

void cxpr_struct_map_clear(cxpr_struct_map* map) {
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        map->entries[i].name = NULL;
        cxpr_struct_value_free(map->entries[i].value);
        map->entries[i].value = NULL;
    }
    map->count = 0;
}

bool cxpr_struct_map_clone(cxpr_struct_map* dst, const cxpr_struct_map* src) {
    cxpr_struct_map_init(dst);
    if (!src || !src->entries || src->count == 0) return true;

    dst->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    dst->entries = (cxpr_struct_map_entry*)calloc(dst->capacity, sizeof(cxpr_struct_map_entry));
    if (!dst->entries) return false;

    for (size_t i = 0; i < src->capacity; i++) {
        cxpr_struct_value* copy;
        cxpr_struct_map_entry* slot;
        if (!src->entries[i].name) continue;
        copy = cxpr_struct_value_new((const char* const*)src->entries[i].value->field_names,
                                     src->entries[i].value->field_values,
                                     src->entries[i].value->field_count);
        if (!copy) return false;
        if ((double)(dst->count + 1) / dst->capacity > CXPR_HASHMAP_LOAD_FACTOR) {
            cxpr_struct_map_grow(dst);
        }
        slot = cxpr_struct_map_find_slot(dst, src->entries[i].name);
        slot->name = strdup(src->entries[i].name);
        slot->value = copy;
        if (!slot->name) return false;
        dst->count++;
    }
    return true;
}

static const cxpr_struct_map_entry* cxpr_struct_map_get(const cxpr_struct_map* map,
                                                        const char* name) {
    cxpr_struct_map_entry* slot = cxpr_struct_map_find_slot(map, name);
    if (!slot || !slot->name) return NULL;
    return slot;
}

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
    if (bucket->entries_base == map->entries && bucket->entry && bucket->entry->key &&
        bucket->hash == hash && strcmp(bucket->entry->key, key) == 0) {
        return bucket->entry;
    }

    entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    if (entry) {
        bucket->key_ref = key;
        bucket->hash = hash;
        bucket->entry = entry;
        bucket->entries_base = map->entries;
        return entry;
    }

    bucket->key_ref = NULL;
    bucket->hash = 0;
    bucket->entry = NULL;
    bucket->entries_base = map->entries;
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
    cxpr_context_entry_cache* bucket;

    if (!map || !cache || !key) return;

    bucket = cxpr_context_cache_bucket(cache, hash);
    bucket->key_ref = key;
    bucket->hash = hash;
    bucket->entry = cxpr_hashmap_find_prehashed_slot(map, key, hash);
    bucket->entries_base = map->entries;
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
    if (!var_clone || !param_clone || !cxpr_struct_map_clone(&clone->structs, &ctx->structs)) {
        free(var_clone);
        free(param_clone);
        cxpr_struct_map_destroy(&clone->structs);
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

void cxpr_context_set(cxpr_context* ctx, const char* name, double value) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (ctx) {
        hash = cxpr_hash_string(name);
        entry = cxpr_context_lookup_cached_entry(&ctx->variables, ctx->variable_cache, name, hash);
        if (entry) {
            entry->value = value;
            return;
        }
        if (cxpr_hashmap_set(&ctx->variables, name, value)) {
            ctx->variables_version++;
        }
        cxpr_context_refresh_cache(&ctx->variables, ctx->variable_cache, name, hash);
    }
}

double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    hash = cxpr_hash_string(name);
    entry = cxpr_context_lookup_cached_entry((cxpr_hashmap*)&ctx->variables,
                                             ((cxpr_context*)ctx)->variable_cache, name, hash);
    if (entry) {
        if (found) *found = true;
        return entry->value;
    }
    if (ctx->parent) return cxpr_context_get(ctx->parent, name, found);
    if (found) *found = false;
    return 0.0;
}

void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (ctx) {
        hash = cxpr_hash_string(name);
        entry = cxpr_context_lookup_cached_entry(&ctx->params, ctx->param_cache, name, hash);
        if (entry) {
            entry->value = value;
            return;
        }
        if (cxpr_hashmap_set(&ctx->params, name, value)) {
            ctx->params_version++;
        }
        cxpr_context_refresh_cache(&ctx->params, ctx->param_cache, name, hash);
    }
}

double cxpr_context_get_param(const cxpr_context* ctx, const char* name, bool* found) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    hash = cxpr_hash_string(name);
    entry = cxpr_context_lookup_cached_entry((cxpr_hashmap*)&ctx->params,
                                             ((cxpr_context*)ctx)->param_cache, name, hash);
    if (entry) {
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
    ctx->variables_version++;
    ctx->params_version++;
}

void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value) {
    cxpr_struct_map_entry* slot;
    cxpr_struct_value* copy;

    if (!ctx || !name || !value) return;

    copy = cxpr_struct_value_new((const char* const*)value->field_names,
                                 value->field_values, value->field_count);
    if (!copy) return;

    if (!ctx->structs.entries) {
        ctx->structs.capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
        ctx->structs.entries =
            (cxpr_struct_map_entry*)calloc(ctx->structs.capacity, sizeof(cxpr_struct_map_entry));
        if (!ctx->structs.entries) {
            ctx->structs.capacity = 0;
            cxpr_struct_value_free(copy);
            return;
        }
    }

    if ((double)(ctx->structs.count + 1) / ctx->structs.capacity > CXPR_HASHMAP_LOAD_FACTOR) {
        cxpr_struct_map_grow(&ctx->structs);
    }

    slot = cxpr_struct_map_find_slot(&ctx->structs, name);
    if (slot->name) {
        cxpr_struct_value_free(slot->value);
        slot->value = copy;
        return;
    }

    slot->name = strdup(name);
    if (!slot->name) {
        cxpr_struct_value_free(copy);
        return;
    }
    slot->value = copy;
    ctx->structs.count++;
}

const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                 const char* name) {
    const cxpr_struct_map_entry* entry;

    if (!ctx) return NULL;

    entry = cxpr_struct_map_get(&ctx->structs, name);
    if (entry) return entry->value;
    return ctx->parent ? cxpr_context_get_struct(ctx->parent, name) : NULL;
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
