#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

void cxpr_context_set_expression_scope(cxpr_context* ctx, const cxpr_evaluator* evaluator);
void cxpr_context_clear_expression_scope(cxpr_context* ctx);

static void test_context_clone_overlay_and_clear(void) {
    cxpr_context* base = cxpr_context_new();
    cxpr_context* child;
    cxpr_context* clone;
    bool found = false;

    assert(base);
    cxpr_context_set(base, "x", 5.0);
    cxpr_context_set_param(base, "len", 14.0);

    clone = cxpr_context_clone(base);
    assert(clone);
    assert(cxpr_context_get(clone, "x", &found) == 5.0 && found);
    assert(cxpr_context_get_param(clone, "len", &found) == 14.0 && found);

    child = cxpr_context_overlay_new(base);
    assert(child);
    assert(cxpr_context_get(child, "x", &found) == 5.0 && found);
    cxpr_context_set(child, "x", 8.0);
    assert(cxpr_context_get(child, "x", &found) == 8.0 && found);
    assert(cxpr_context_get(base, "x", &found) == 5.0 && found);

    cxpr_context_clear(child);
    (void)cxpr_context_get(child, "x", &found);
    assert(found == true);
    assert(cxpr_context_get(child, "x", &found) == 5.0 && found);

    cxpr_context_free(child);
    cxpr_context_free(clone);
    cxpr_context_free(base);
}

static void test_context_expression_scope_clear_accepts_nulls(void) {
    cxpr_context* ctx = cxpr_context_new();

    assert(ctx);
    cxpr_context_set_expression_scope(ctx, NULL);
    cxpr_context_clear_expression_scope(ctx);
    cxpr_context_set_expression_scope(NULL, NULL);
    cxpr_context_clear_expression_scope(NULL);
    cxpr_context_free(ctx);
}

static void test_used_overlay_pool_resets_values_and_parent(void) {
    cxpr_context* first_parent = cxpr_context_new();
    cxpr_context* second_parent = cxpr_context_new();
    cxpr_context* overlay;
    bool found = false;

    assert(first_parent && second_parent);
    cxpr_context_set(first_parent, "parent_value", 1.0);
    cxpr_context_set(second_parent, "parent_value", 2.0);

    overlay = cxpr_context_overlay_new(first_parent);
    assert(overlay);
    cxpr_context_set(overlay, "local_value", 7.0);
    cxpr_context_free(overlay);

    overlay = cxpr_context_overlay_new(second_parent);
    assert(overlay);
    (void)cxpr_context_get(overlay, "local_value", &found);
    assert(!found);
    assert(cxpr_context_get(overlay, "parent_value", &found) == 2.0 && found);

    cxpr_context_free(overlay);
    cxpr_context_free(second_parent);
    cxpr_context_free(first_parent);
}

int main(void) {
    test_context_clone_overlay_and_clear();
    test_context_expression_scope_clear_accepts_nulls();
    test_used_overlay_pool_resets_values_and_parent();
    printf("  \xE2\x9C\x93 context_lifecycle\n");
    return 0;
}
