#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_context_slot_binding_and_invalidation(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_slot slot = {0};
    bool found = false;

    assert(ctx);
    cxpr_context_set(ctx, "price", 10.0);
    assert(cxpr_context_slot_bind(ctx, "price", &slot));
    assert(cxpr_context_slot_valid(ctx, &slot));
    assert(cxpr_context_slot_get(&slot) == 10.0);
    cxpr_context_slot_set(&slot, 12.5);
    assert(cxpr_context_slot_get(&slot) == 12.5);
    assert(cxpr_context_get(ctx, "price", &found) == 12.5);
    assert(found);
    assert(!cxpr_context_slot_bind(ctx, "missing", &slot));

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_slot_binding_and_invalidation();
    printf("  \xE2\x9C\x93 context_slot\n");
    return 0;
}
