#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include "../src/hashmap/internal.h"

static void test_context_value_paths(void) {
    cxpr_context* ctx = cxpr_context_new();
    cxpr_context_entry vars[] = {{"a", 1.0}, {"b", 2.0}, {NULL, 0.0}};
    cxpr_context_entry params[] = {{"len", 14.0}, {NULL, 0.0}};
    bool found = false;
    cxpr_value value;

    assert(ctx);
    cxpr_context_set_array(ctx, vars);
    cxpr_context_set_param_array(ctx, params);
    cxpr_context_set_prehashed(ctx, "c", cxpr_hash_string("c"), 3.0);
    cxpr_context_set_param_prehashed(ctx, "mult", cxpr_hash_string("mult"), 4.0);
    cxpr_context_set_bool(ctx, "flag", true);

    assert(cxpr_context_get(ctx, "a", &found) == 1.0 && found);
    assert(cxpr_context_get(ctx, "c", &found) == 3.0 && found);
    assert(cxpr_context_get_param(ctx, "len", &found) == 14.0 && found);
    assert(cxpr_context_get_param(ctx, "mult", &found) == 4.0 && found);

    value = cxpr_context_get_typed(ctx, "b", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 2.0);

    value = cxpr_context_get_typed(ctx, "flag", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_BOOL);
    assert(value.b == true);
    assert(cxpr_context_get(ctx, "flag", &found) == 1.0 && found);
    assert(cxpr_context_get_bool(ctx, "flag", &found) == true && found);

    cxpr_context_set(ctx, "flag", 5.0);
    value = cxpr_context_get_typed(ctx, "flag", &found);
    assert(found);
    assert(value.type == CXPR_VALUE_NUMBER);
    assert(value.d == 5.0);
    assert(cxpr_context_get_bool(ctx, "flag", &found) == false && !found);

    cxpr_context_set_bool(ctx, "parent_flag", true);
    {
        cxpr_context* overlay = cxpr_context_overlay_new(ctx);
        assert(overlay);
        cxpr_context_set(overlay, "parent_flag", 7.0);
        value = cxpr_context_get_typed(overlay, "parent_flag", &found);
        assert(found);
        assert(value.type == CXPR_VALUE_NUMBER);
        assert(value.d == 7.0);
        cxpr_context_free(overlay);
    }

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_value_paths();
    printf("  \xE2\x9C\x93 context_values\n");
    return 0;
}
