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

static void test_context_slots_bind_and_set(void) {
    cxpr_context* ctx = cxpr_context_new();
    const char* names[] = {"open", "close", "volume"};
    double values[] = {11.0, 12.5, 2500.0};
    cxpr_context_slot slots[3];
    bool found = false;

    assert(ctx);
    cxpr_context_set(ctx, "open", 0.0);
    cxpr_context_set(ctx, "close", 0.0);
    cxpr_context_set(ctx, "volume", 0.0);

    assert(cxpr_context_slots_bind(ctx, names, slots, CXPR_ARRAY_COUNT(slots)));
    assert(cxpr_context_slot_valid(ctx, &slots[0]));
    assert(cxpr_context_slot_valid(ctx, &slots[1]));
    assert(cxpr_context_slot_valid(ctx, &slots[2]));

    cxpr_context_slots_set(slots, values, CXPR_ARRAY_COUNT(slots));
    assert(cxpr_context_get(ctx, "open", &found) == 11.0);
    assert(found);
    assert(cxpr_context_get(ctx, "close", &found) == 12.5);
    assert(found);
    assert(cxpr_context_get(ctx, "volume", &found) == 2500.0);
    assert(found);

    cxpr_context_free(ctx);
}

static void test_context_slots_bind_clears_outputs_on_miss(void) {
    cxpr_context* ctx = cxpr_context_new();
    const char* names[] = {"open", "missing", "close"};
    cxpr_context_slot slots[3] = {{0}};

    assert(ctx);
    cxpr_context_set(ctx, "open", 0.0);
    cxpr_context_set(ctx, "close", 0.0);

    assert(!cxpr_context_slots_bind(ctx, names, slots, CXPR_ARRAY_COUNT(slots)));
    assert(!cxpr_context_slot_valid(ctx, &slots[0]));
    assert(!cxpr_context_slot_valid(ctx, &slots[1]));
    assert(!cxpr_context_slot_valid(ctx, &slots[2]));

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_slot_binding_and_invalidation();
    test_context_slots_bind_and_set();
    test_context_slots_bind_clears_outputs_on_miss();
    printf("  \xE2\x9C\x93 context_slot\n");
    return 0;
}
