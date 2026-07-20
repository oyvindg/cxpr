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

static cxpr_string_map_entry* cxpr_string_map_find(cxpr_string_map* map, const char* name) {
    if (!map || !name) return NULL;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) return &map->entries[i];
    }
    return NULL;
}

static bool cxpr_string_map_set(cxpr_string_map* map, const char* name, const char* value) {
    cxpr_string_map_entry* entry;
    cxpr_string_map_entry* grown;
    char* value_copy;
    size_t new_capacity;

    if (!map || !name) return false;
    value_copy = cxpr_strdup(value ? value : "");
    if (!value_copy) return false;

    entry = cxpr_string_map_find(map, name);
    if (entry) {
        free(entry->value);
        entry->value = value_copy;
        return true;
    }

    if (map->count == map->capacity) {
        new_capacity = map->capacity == 0u ? 8u : map->capacity * 2u;
        grown = (cxpr_string_map_entry*)realloc(
            map->entries, new_capacity * sizeof(cxpr_string_map_entry));
        if (!grown) {
            free(value_copy);
            return false;
        }
        map->entries = grown;
        map->capacity = new_capacity;
    }

    map->entries[map->count].name = cxpr_strdup(name);
    if (!map->entries[map->count].name) {
        free(value_copy);
        return false;
    }
    map->entries[map->count].value = value_copy;
    map->count++;
    return true;
}

static void cxpr_string_map_remove(cxpr_string_map* map, const char* name) {
    if (!map || !name) return;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) {
            free(map->entries[i].name);
            free(map->entries[i].value);
            if (i + 1u < map->count) {
                memmove(&map->entries[i], &map->entries[i + 1u],
                        (map->count - i - 1u) * sizeof(cxpr_string_map_entry));
            }
            map->count--;
            return;
        }
    }
}

static const char* cxpr_string_map_get(const cxpr_string_map* map, const char* name,
                                       bool* found) {
    if (found) *found = false;
    if (!map || !name) return NULL;
    for (size_t i = 0u; i < map->count; ++i) {
        if (strcmp(map->entries[i].name, name) == 0) {
            if (found) *found = true;
            return map->entries[i].value;
        }
    }
    return NULL;
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
    if (ctx && name) cxpr_string_map_remove(&ctx->strings, name);
    if (ctx && name) cxpr_context_remove_array(&ctx->arrays, name);
    if (ctx && name) cxpr_context_remove_struct(&ctx->structs, name);
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
    cxpr_string_map_remove(&ctx->strings, name);
    cxpr_context_remove_array(&ctx->arrays, name);
    cxpr_context_remove_struct(&ctx->structs, name);
    if (cxpr_bool_map_set(&ctx->bools, name, value)) ctx->variables_version++;
}

void cxpr_context_set_string(cxpr_context* ctx, const char* name, const char* value) {
    if (!ctx || !name) return;
    cxpr_bool_map_remove(&ctx->bools, name);
    cxpr_context_remove_array(&ctx->arrays, name);
    cxpr_context_remove_struct(&ctx->structs, name);
    if (cxpr_string_map_set(&ctx->strings, name, value)) ctx->variables_version++;
}

void cxpr_context_set_value(cxpr_context* ctx, const char* name, const cxpr_value* value) {
    if (!ctx || !name || !value) return;

    switch (value->type) {
    case CXPR_VALUE_NUMBER:
        cxpr_context_set(ctx, name, value->d);
        return;
    case CXPR_VALUE_BOOL:
        cxpr_context_set_bool(ctx, name, value->b);
        return;
    case CXPR_VALUE_STRING:
        cxpr_context_set_string(ctx, name, value->str);
        return;
    case CXPR_VALUE_STRUCT:
        cxpr_bool_map_remove(&ctx->bools, name);
        cxpr_string_map_remove(&ctx->strings, name);
        cxpr_context_remove_array(&ctx->arrays, name);
        cxpr_context_set_struct(ctx, name, value->s);
        return;
    case CXPR_VALUE_ARRAY:
        if (!value->a) return;
        cxpr_bool_map_remove(&ctx->bools, name);
        cxpr_string_map_remove(&ctx->strings, name);
        cxpr_context_remove_struct(&ctx->structs, name);
        cxpr_context_store_array(&ctx->arrays, name, value->a);
        ctx->variables_version++;
        return;
    default:
        return;
    }
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

const char* cxpr_context_get_string(const cxpr_context* ctx, const char* name, bool* found) {
    bool local_found = false;
    const char* value;

    if (!ctx || !name) {
        if (found) *found = false;
        return NULL;
    }

    value = cxpr_string_map_get(&ctx->strings, name, &local_found);
    if (local_found) {
        if (found) *found = true;
        return value;
    }
    if (ctx->parent) return cxpr_context_get_string(ctx->parent, name, found);
    if (found) *found = false;
    return NULL;
}

bool cxpr_context_get_local_bool(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }
    return cxpr_bool_map_get(&ctx->bools, name, found);
}

const char* cxpr_context_get_local_string(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return NULL;
    }
    return cxpr_string_map_get(&ctx->strings, name, found);
}

void cxpr_context_set_param_prehashed(cxpr_context* ctx, const char* name,
                                      unsigned long hash, double value) {
    if (ctx && name) cxpr_bool_map_remove(&ctx->bool_params, name);
    if (ctx && name) cxpr_string_map_remove(&ctx->string_params, name);
    if (ctx && name) cxpr_context_remove_array(&ctx->array_params, name);
    if (ctx && name) cxpr_context_remove_struct(&ctx->structs, name);
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
    cxpr_string_map_remove(&ctx->string_params, name);
    cxpr_context_remove_array(&ctx->array_params, name);
    cxpr_context_remove_struct(&ctx->structs, name);
    if (cxpr_bool_map_set(&ctx->bool_params, name, value)) ctx->params_version++;
}

void cxpr_context_set_param_string(cxpr_context* ctx, const char* name, const char* value) {
    if (!ctx || !name) return;
    cxpr_bool_map_remove(&ctx->bool_params, name);
    cxpr_context_remove_array(&ctx->array_params, name);
    cxpr_context_remove_struct(&ctx->structs, name);
    if (cxpr_string_map_set(&ctx->string_params, name, value)) ctx->params_version++;
}

void cxpr_context_set_param_value(cxpr_context* ctx, const char* name, const cxpr_value* value) {
    if (!ctx || !name || !value) return;

    switch (value->type) {
    case CXPR_VALUE_NUMBER:
        cxpr_context_set_param(ctx, name, value->d);
        return;
    case CXPR_VALUE_BOOL:
        cxpr_context_set_param_bool(ctx, name, value->b);
        return;
    case CXPR_VALUE_STRING:
        cxpr_context_set_param_string(ctx, name, value->str);
        return;
    case CXPR_VALUE_STRUCT:
        cxpr_bool_map_remove(&ctx->bool_params, name);
        cxpr_string_map_remove(&ctx->string_params, name);
        cxpr_context_remove_array(&ctx->array_params, name);
        if (value->s) {
            cxpr_context_store_struct(&ctx->structs, name, value->s);
            ctx->params_version++;
        }
        return;
    case CXPR_VALUE_ARRAY:
        if (!value->a) return;
        cxpr_bool_map_remove(&ctx->bool_params, name);
        cxpr_string_map_remove(&ctx->string_params, name);
        cxpr_context_remove_struct(&ctx->structs, name);
        cxpr_context_store_array(&ctx->array_params, name, value->a);
        ctx->params_version++;
        return;
    default:
        return;
    }
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

const char* cxpr_context_get_param_string(const cxpr_context* ctx, const char* name, bool* found) {
    bool local_found = false;
    const char* value;

    if (!ctx || !name) {
        if (found) *found = false;
        return NULL;
    }

    value = cxpr_string_map_get(&ctx->string_params, name, &local_found);
    if (local_found) {
        if (found) *found = true;
        return value;
    }
    if (ctx->parent) return cxpr_context_get_param_string(ctx->parent, name, found);
    if (found) *found = false;
    return NULL;
}

bool cxpr_context_get_local_param_bool(const cxpr_context* ctx, const char* name, bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return false;
    }
    return cxpr_bool_map_get(&ctx->bool_params, name, found);
}

const char* cxpr_context_get_local_param_string(const cxpr_context* ctx, const char* name,
                                                bool* found) {
    if (!ctx || !name) {
        if (found) *found = false;
        return NULL;
    }
    return cxpr_string_map_get(&ctx->string_params, name, found);
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
