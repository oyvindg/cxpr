/**
 * @file values.c
 * @brief Numeric variable and parameter bindings.
 */

#include "internal.h"

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

void cxpr_context_set_param_array(cxpr_context* ctx, const cxpr_context_entry* entries) {
    size_t i;

    if (!ctx || !entries) return;

    for (i = 0; entries[i].name; ++i) {
        cxpr_context_set_param(ctx, entries[i].name, entries[i].value);
    }
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
