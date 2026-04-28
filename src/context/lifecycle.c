/**
 * @file lifecycle.c
 * @brief Context allocation, cloning, and reset routines.
 */

#include "internal.h"

cxpr_context* cxpr_context_new(void) {
    cxpr_context* ctx = (cxpr_context*)calloc(1, sizeof(cxpr_context));
    if (!ctx) return NULL;
    cxpr_hashmap_init(&ctx->variables);
    cxpr_hashmap_init(&ctx->params);
    cxpr_struct_map_init(&ctx->structs);
    cxpr_struct_map_init(&ctx->cached_structs);
    ctx->eval_memo.entries = NULL;
    ctx->eval_memo.capacity = 0u;
    ctx->eval_memo.count = 0u;
    ctx->eval_memo.depth = 0u;
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
    cxpr_hashmap_destroy(&ctx->variables);
    cxpr_hashmap_destroy(&ctx->params);
    cxpr_struct_map_destroy(&ctx->structs);
    cxpr_struct_map_destroy(&ctx->cached_structs);
    free(ctx->eval_memo.entries);
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
    clone->eval_memo.entries = NULL;
    clone->eval_memo.capacity = 0u;
    clone->eval_memo.count = 0u;
    clone->eval_memo.depth = 0u;
    clone->parent = NULL;
    free(var_clone);
    free(param_clone);
    return clone;
}

void cxpr_context_clear(cxpr_context* ctx) {
    if (!ctx) return;
    cxpr_hashmap_clear(&ctx->variables);
    cxpr_hashmap_clear(&ctx->params);
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
