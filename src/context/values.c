/**
 * @file values.c
 * @brief Numeric variable and parameter bindings.
 */

#include "internal.h"

static cxpr_bool_map_entry* cxpr_bool_map_find(cxpr_bool_map* map, const char* name) {
    if (!map || !name) return NULL;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) return &map->entries[i];
    }
    return NULL;
}

static bool cxpr_bool_map_set(cxpr_bool_map* map, const char* name, bool value) {
    cxpr_bool_map_entry* entry;
    cxpr_bool_map_entry* grown;
    size_t new_capacity;

    if (!map || !name) return false;
    entry = cxpr_bool_map_find(map, name);
    if (entry) {
        entry->value = value;
        return true;
    }
    if (map->count == map->capacity) {
        new_capacity = map->capacity == 0u ? 8u : map->capacity * 2u;
        grown = (cxpr_bool_map_entry*)realloc(
            map->entries, new_capacity * sizeof(cxpr_bool_map_entry));
        if (!grown) return false;
        map->entries = grown;
        map->capacity = new_capacity;
    }
    map->entries[map->count].name = cxpr_strdup(name);
    if (!map->entries[map->count].name) return false;
    map->entries[map->count].value = value;
    map->count++;
    return true;
}

static void cxpr_bool_map_remove(cxpr_bool_map* map, const char* name) {
    if (!map || !name) return;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) {
            free(map->entries[i].name);
            if (i + 1u < map->count) {
                memmove(&map->entries[i], &map->entries[i + 1u],
                        (map->count - i - 1u) * sizeof(cxpr_bool_map_entry));
            }
            map->count--;
            return;
        }
    }
}

static bool cxpr_bool_map_get(const cxpr_bool_map* map, const char* name, bool* found) {
    if (found) *found = false;
    if (!map || !name) return false;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) {
            if (found) *found = true;
            return map->entries[i].value;
        }
    }
    return false;
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
    if (ctx && name) cxpr_bool_map_remove(&ctx->bools, name);
    cxpr_context_set_hashed(ctx, &ctx->variables, ctx->variable_cache,
                            ctx->variable_ptr_cache, &ctx->variables_version,
                            name, hash, value);
}

void cxpr_context_set(cxpr_context* ctx, const char* name, double value) {
    if (!ctx || !name) return;
    cxpr_context_set_prehashed(ctx, name, cxpr_hash_string(name), value);
}

void cxpr_context_set_bool(cxpr_context* ctx, const char* name, bool value) {
    if (!ctx || !name) return;
    if (cxpr_bool_map_set(&ctx->bools, name, value)) ctx->variables_version++;
}

void cxpr_context_set_array(cxpr_context* ctx, const cxpr_context_entry* entries) {
    size_t i;

    if (!ctx || !entries) return;

    for (i = 0; entries[i].name; ++i) {
        cxpr_context_set(ctx, entries[i].name, entries[i].value);
    }
}

double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found) {
    cxpr_value typed;

    if (!ctx) {
        if (found) *found = false;
        return 0.0;
    }

    typed = cxpr_context_get_typed(ctx, name, found);
    if (found && *found) {
        if (typed.type == CXPR_VALUE_NUMBER) return typed.d;
        if (typed.type == CXPR_VALUE_BOOL) return typed.b ? 1.0 : 0.0;
        *found = false;
        return 0.0;
    }
    if (found) *found = false;
    return 0.0;
}

bool cxpr_context_get_bool(const cxpr_context* ctx, const char* name, bool* found) {
    bool local_found = false;
    bool value;

    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }

    value = cxpr_bool_map_get(&ctx->bools, name, &local_found);
    if (local_found) {
        if (found) *found = true;
        return value;
    }
    if (ctx->parent) return cxpr_context_get_bool(ctx->parent, name, found);
    if (found) *found = false;
    return false;
}

bool cxpr_context_get_local_bool(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }
    return cxpr_bool_map_get(&ctx->bools, name, found);
}

void cxpr_context_set_param_prehashed(cxpr_context* ctx, const char* name,
                                      unsigned long hash, double value) {
    if (ctx && name) cxpr_bool_map_remove(&ctx->bool_params, name);
    cxpr_context_set_hashed(ctx, &ctx->params, ctx->param_cache,
                            ctx->param_ptr_cache, &ctx->params_version,
                            name, hash, value);
}

void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value) {
    if (!ctx || !name) return;
    cxpr_context_set_param_prehashed(ctx, name, cxpr_hash_string(name), value);
}

void cxpr_context_set_param_array(cxpr_context* ctx, const cxpr_context_entry* entries) {
    size_t i;

    if (!ctx || !entries) return;

    for (i = 0; entries[i].name; ++i) {
        cxpr_context_set_param(ctx, entries[i].name, entries[i].value);
    }
}

void cxpr_context_set_param_bool(cxpr_context* ctx, const char* name, bool value) {
    if (!ctx || !name) return;
    if (cxpr_bool_map_set(&ctx->bool_params, name, value)) ctx->params_version++;
}

bool cxpr_context_get_param_bool(const cxpr_context* ctx, const char* name, bool* found) {
    bool local_found = false;
    bool value;

    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }

    value = cxpr_bool_map_get(&ctx->bool_params, name, &local_found);
    if (local_found) {
        if (found) *found = true;
        return value;
    }
    if (ctx->parent) return cxpr_context_get_param_bool(ctx->parent, name, found);
    if (found) *found = false;
    return false;
}

bool cxpr_context_get_local_param_bool(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }
    return cxpr_bool_map_get(&ctx->bool_params, name, found);
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
