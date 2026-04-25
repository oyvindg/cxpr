#include <assert.h>
#include <stdio.h>
#include "../src/hashmap/internal.h"

static void test_hashmap_internal_operations(void) {
    cxpr_hashmap map;
    cxpr_hashmap* clone;
    bool found = false;
    unsigned long hash;

    cxpr_hashmap_init(&map);
    assert(map.capacity == CXPR_HASHMAP_INITIAL_CAPACITY);

    assert(cxpr_hashmap_set(&map, "alpha", 1.5));
    assert(!cxpr_hashmap_set(&map, "alpha", 2.5));
    hash = cxpr_hash_string("beta");
    assert(cxpr_hashmap_set_prehashed(&map, "beta", hash, 3.5));

    assert(cxpr_hashmap_get(&map, "alpha", &found) == 2.5);
    assert(found);
    assert(cxpr_hashmap_get_prehashed(&map, "beta", hash, &found) == 3.5);
    assert(found);
    assert(cxpr_hashmap_find_prehashed_entry(&map, "beta", hash) != NULL);
    assert(cxpr_hashmap_find_prehashed_slot(&map, "beta", hash) != NULL);

    clone = cxpr_hashmap_clone(&map);
    assert(clone);
    assert(cxpr_hashmap_get(clone, "alpha", &found) == 2.5);
    assert(found);

    cxpr_hashmap_clear(&map);
    assert(map.count == 0);

    cxpr_hashmap_destroy(clone);
    free(clone);
    cxpr_hashmap_destroy(&map);
}

int main(void) {
    test_hashmap_internal_operations();
    printf("  \xE2\x9C\x93 hashmap\n");
    return 0;
}
