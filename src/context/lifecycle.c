/**
 * @file lifecycle.c
 * @brief Context allocation, cloning, and reset routines.
 */

#include "internal.h"

#define CXPR_OVERLAY_CONTEXT_CACHE_MAX 64u

static cxpr_context* cxpr_overlay_context_cache = NULL;
static size_t cxpr_overlay_context_cache_count = 0u;
static bool cxpr_overlay_context_cache_atexit_registered = false;

static void cxpr_context_destroy_storage(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_destroy(&ctx->variables);
    cxpr_hashmap_destroy(&ctx->params);
    for (size_t i = 0u; i < ctx->bools.count; ++i) free(ctx->bools.entries[i].name);
    free(ctx->bools.entries);
    for (size_t i = 0u; i < ctx->bool_params.count; ++i) free(ctx->bool_params.entries[i].name);
    free(ctx->bool_params.entries);
    cxpr_struct_map_destroy(&ctx->structs);
    cxpr_struct_map_destroy(&ctx->cached_structs);
    free(ctx->eval_memo.entries);
    free(ctx);
}

static void cxpr_context_reset_empty_overlay(cxpr_context* ctx, const cxpr_context* parent) {
    if (!ctx) return;
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
    ctx->variables_version = 1;
    ctx->params_version = 1;
    ctx->parent = parent;
    ctx->expression_scope = NULL;
    ctx->overlay_cache_next = NULL;
}

static bool cxpr_context_can_cache_empty_overlay(const cxpr_context* ctx) {
    return ctx && ctx->parent &&
           ctx->variables_version == 1 &&
           ctx->params_version == 1 &&
           ctx->variables.count == 0u &&
           ctx->params.count == 0u &&
           ctx->bools.count == 0u &&
           ctx->bool_params.count == 0u &&
           ctx->bools.entries == NULL &&
           ctx->bool_params.entries == NULL &&
           ctx->structs.count == 0u &&
           ctx->cached_structs.count == 0u &&
           ctx->structs.entries == NULL &&
           ctx->cached_structs.entries == NULL &&
           ctx->eval_memo.count == 0u &&
           ctx->eval_memo.depth == 0u &&
           ctx->eval_memo.entries == NULL &&
           ctx->expression_scope == NULL;
}

static void cxpr_context_overlay_cache_destroy(void) {
    while (cxpr_overlay_context_cache) {
        cxpr_context* next = cxpr_overlay_context_cache->overlay_cache_next;
        cxpr_overlay_context_cache->overlay_cache_next = NULL;
        cxpr_context_destroy_storage(cxpr_overlay_context_cache);
        cxpr_overlay_context_cache = next;
    }
    cxpr_overlay_context_cache_count = 0u;
}

cxpr_context* cxpr_context_new(void) {
    cxpr_context* ctx = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!ctx) return NULL;
    cxpr_hashmap_init(&ctx->variables);
    cxpr_hashmap_init(&ctx->params);
    ctx->bools.entries = NULL;
    ctx->bools.capacity = 0u;
    ctx->bools.count = 0u;
    ctx->bool_params.entries = NULL;
    ctx->bool_params.capacity = 0u;
    ctx->bool_params.count = 0u;
    cxpr_struct_map_init(&ctx->structs);
    cxpr_struct_map_init(&ctx->cached_structs);
    ctx->eval_memo.entries = NULL;
    ctx->eval_memo.capacity = 0u;
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
    ctx->variables_version = 1;
    ctx->params_version = 1;
    ctx->parent = NULL;
    ctx->overlay_cache_next = NULL;
    return ctx;
}

cxpr_context* cxpr_context_overlay_new(const cxpr_context* parent) {
    cxpr_context* ctx;

    if (cxpr_overlay_context_cache) {
        ctx = cxpr_overlay_context_cache;
        cxpr_overlay_context_cache = ctx->overlay_cache_next;
        cxpr_overlay_context_cache_count--;
        cxpr_context_reset_empty_overlay(ctx, parent);
        return ctx;
    }

    ctx = cxpr_context_new();
    if (!ctx) return NULL;
    ctx->parent = parent;
    return ctx;
}

void cxpr_context_set_expression_scope(cxpr_context* ctx, const cxpr_evaluator* evaluator) {
    if (!ctx) return;
    ctx->expression_scope = evaluator;
}

void cxpr_context_clear_expression_scope(cxpr_context* ctx) {
    if (!ctx) return;
    ctx->expression_scope = NULL;
}

void cxpr_context_free(cxpr_context* ctx) {
    if (!ctx) return;
    if (cxpr_context_can_cache_empty_overlay(ctx) &&
        cxpr_overlay_context_cache_count < CXPR_OVERLAY_CONTEXT_CACHE_MAX) {
        if (!cxpr_overlay_context_cache_atexit_registered) {
            atexit(cxpr_context_overlay_cache_destroy);
            cxpr_overlay_context_cache_atexit_registered = true;
        }
        cxpr_context_reset_empty_overlay(ctx, NULL);
        ctx->overlay_cache_next = cxpr_overlay_context_cache;
        cxpr_overlay_context_cache = ctx;
        cxpr_overlay_context_cache_count++;
        return;
    }
    cxpr_context_destroy_storage(ctx);
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
    if (ctx->bools.count > 0u) {
        clone->bools.entries =
            (cxpr_bool_map_entry*)calloc(ctx->bools.count, sizeof(cxpr_bool_map_entry));
        if (!clone->bools.entries) {
            cxpr_hashmap_destroy(&clone->variables);
            cxpr_hashmap_destroy(&clone->params);
            cxpr_struct_map_destroy(&clone->structs);
            cxpr_struct_map_destroy(&clone->cached_structs);
            free(var_clone);
            free(param_clone);
            free(clone);
            return NULL;
        }
        clone->bools.capacity = ctx->bools.count;
        for (size_t i = 0u; i < ctx->bools.count; ++i) {
            clone->bools.entries[i].name = cxpr_strdup(ctx->bools.entries[i].name);
            if (!clone->bools.entries[i].name) {
                cxpr_context_free(clone);
                free(var_clone);
                free(param_clone);
                return NULL;
            }
            clone->bools.entries[i].value = ctx->bools.entries[i].value;
            clone->bools.count++;
        }
    }
    if (ctx->bool_params.count > 0u) {
        clone->bool_params.entries =
            (cxpr_bool_map_entry*)calloc(ctx->bool_params.count, sizeof(cxpr_bool_map_entry));
        if (!clone->bool_params.entries) {
            cxpr_context_free(clone);
            free(var_clone);
            free(param_clone);
            return NULL;
        }
        clone->bool_params.capacity = ctx->bool_params.count;
        for (size_t i = 0u; i < ctx->bool_params.count; ++i) {
            clone->bool_params.entries[i].name = cxpr_strdup(ctx->bool_params.entries[i].name);
            if (!clone->bool_params.entries[i].name) {
                cxpr_context_free(clone);
                free(var_clone);
                free(param_clone);
                return NULL;
            }
            clone->bool_params.entries[i].value = ctx->bool_params.entries[i].value;
            clone->bool_params.count++;
        }
    }
    clone->variables_version = ctx->variables_version;
    clone->params_version = ctx->params_version;
    clone->eval_memo.entries = NULL;
    clone->eval_memo.capacity = 0u;
    clone->eval_memo.count = 0u;
    clone->eval_memo.depth = 0u;
    clone->parent = NULL;
    clone->expression_scope = ctx->expression_scope;
    clone->overlay_cache_next = NULL;
    free(var_clone);
    free(param_clone);
    return clone;
}

void cxpr_context_clear(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_clear(&ctx->variables);
    cxpr_hashmap_clear(&ctx->params);
    for (size_t i = 0u; i < ctx->bools.count; ++i) free(ctx->bools.entries[i].name);
    ctx->bools.count = 0u;
    for (size_t i = 0u; i < ctx->bool_params.count; ++i) free(ctx->bool_params.entries[i].name);
    ctx->bool_params.count = 0u;
    cxpr_struct_map_clear(&ctx->structs);
    cxpr_struct_map_clear(&ctx->cached_structs);
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
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
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
}
