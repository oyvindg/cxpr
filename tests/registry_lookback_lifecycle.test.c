#include <cxpr/cxpr.h>

#include <assert.h>
#include <stdlib.h>

typedef struct {
    size_t* frees;
} owned_userdata;

static bool unused_resolver(const cxpr_expr_ast* target,
                            const cxpr_expr_ast* index,
                            const cxpr_context* context,
                            const cxpr_registry* registry,
                            void* userdata,
                            cxpr_value* out,
                            cxpr_error* error) {
    (void)target;
    (void)index;
    (void)context;
    (void)registry;
    (void)userdata;
    (void)out;
    (void)error;
    return false;
}

static void count_free(void* userdata) {
    owned_userdata* owned = (owned_userdata*)userdata;
    ++*owned->frees;
    free(owned);
}

static owned_userdata* make_userdata(size_t* frees) {
    owned_userdata* owned = (owned_userdata*)malloc(sizeof(*owned));
    assert(owned);
    owned->frees = frees;
    return owned;
}

static void test_replacement_and_registry_free_each_clean_once(void) {
    size_t frees = 0u;
    cxpr_registry* registry = cxpr_registry_new();
    owned_userdata* first = make_userdata(&frees);
    owned_userdata* second = make_userdata(&frees);
    assert(registry);

    cxpr_registry_set_lookback_resolver(
        registry, unused_resolver, first, count_free);
    cxpr_registry_set_lookback_resolver(
        registry, unused_resolver, second, count_free);
    assert(frees == 1u);

    /* A borrowed re-registration of the same pointer retains prior ownership. */
    cxpr_registry_set_lookback_resolver(
        registry, unused_resolver, second, NULL);
    assert(frees == 1u);
    cxpr_registry_free(registry);
    assert(frees == 2u);
}

static void test_clearing_resolver_releases_owned_userdata(void) {
    size_t frees = 0u;
    cxpr_registry* registry = cxpr_registry_new();
    assert(registry);
    cxpr_registry_set_lookback_resolver(
        registry, unused_resolver, make_userdata(&frees), count_free);
    cxpr_registry_set_lookback_resolver(registry, NULL, NULL, NULL);
    assert(frees == 1u);
    cxpr_registry_free(registry);
    assert(frees == 1u);
}

int main(void) {
    test_replacement_and_registry_free_each_clean_once();
    test_clearing_resolver_releases_owned_userdata();
    return 0;
}
