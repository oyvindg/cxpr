#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include "../src/core.h" // IWYU pragma: keep
#include "../src/context/internal.h"

static void test_context_entry_cache_helpers(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_hashmap_entry* entry;
    unsigned long hash = cxpr_hash_string("alpha");

    assert(ctx);
    cxpr_context_set_prehashed(ctx, "alpha", hash, 2.0);
    cxpr_context_clear_entry_cache(ctx->variable_cache);
    entry = cxpr_context_lookup_cached_entry(&ctx->variables, ctx->variable_cache, "alpha", hash);
    assert(entry);
    assert(entry->value == 2.0);
    assert(ctx->variable_cache[hash & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)].key_ref != NULL);

    cxpr_context_refresh_cache(&ctx->variables, ctx->variable_cache, "alpha", hash);
    assert(ctx->variable_cache[hash & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)].entries_base ==
           ctx->variables.entries);

    cxpr_context_clear_entry_cache(ctx->variable_cache);
    assert(ctx->variable_cache[hash & (CXPR_CONTEXT_ENTRY_CACHE_SIZE - 1)].key_ref == NULL);

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_entry_cache_helpers();
    printf("  \xE2\x9C\x93 context_cache\n");
    return 0;
}
