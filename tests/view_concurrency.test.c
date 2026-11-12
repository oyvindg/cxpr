#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include <cxpr/view_lease.h>

enum {
    READER_COUNT = 4,
    READER_ITERATIONS = 50000,
    PUBLICATION_ITERATIONS = 25000
};

typedef struct publication_fixture {
    cxpr_view_table generation_a;
    cxpr_view_table generation_b;
    _Atomic(const cxpr_view_table*) published;
    atomic_uint ready;
    atomic_bool start;
    atomic_uint failures;
    atomic_uint_fast64_t reads;
} publication_fixture;

static void* reader_main(void* userdata) {
    publication_fixture* fixture = userdata;
    atomic_fetch_add_explicit(&fixture->ready, 1u, memory_order_release);
    while (!atomic_load_explicit(&fixture->start, memory_order_acquire))
        sched_yield();
    for (size_t iteration = 0u; iteration < READER_ITERATIONS; ++iteration) {
        const cxpr_view_table* table = atomic_load_explicit(
            &fixture->published, memory_order_acquire);
        cxpr_view_execution_status status;
        cxpr_view_lease lease;
        cxpr_value value = cxpr_num(0.0);
        const double index = 1.0;
        const double expected = table == &fixture->generation_a ? 12.0 : 22.0;
        cxpr_view_execution_status_reset(&status);
        if (cxpr_view_lease_acquire(
                table, table->generation, &status, &lease) != CXPR_VIEW_OK ||
            cxpr_view_lease_load_contained(
                &lease, 0u, &index, 1u, iteration, &value) != CXPR_VIEW_OK ||
            value.type != CXPR_VALUE_NUMBER || value.d != expected ||
            status.code != CXPR_VIEW_OK) {
            atomic_fetch_add_explicit(
                &fixture->failures, 1u, memory_order_relaxed);
        }
        atomic_fetch_add_explicit(&fixture->reads, 1u, memory_order_relaxed);
    }
    return NULL;
}

static void* publisher_main(void* userdata) {
    publication_fixture* fixture = userdata;
    while (atomic_load_explicit(&fixture->ready, memory_order_acquire) !=
           READER_COUNT) sched_yield();
    atomic_store_explicit(&fixture->start, true, memory_order_release);
    for (size_t iteration = 0u; iteration < PUBLICATION_ITERATIONS; ++iteration) {
        const cxpr_view_table* next = (iteration & 1u)
            ? &fixture->generation_a : &fixture->generation_b;
        atomic_store_explicit(&fixture->published, next, memory_order_release);
        if ((iteration & 63u) == 0u) sched_yield();
    }
    return NULL;
}

static void test_concurrent_immutable_publication(void) {
    static const double values_a[] = {11.0, 12.0};
    static const double values_b[] = {21.0, 22.0};
    static const cxpr_view_resolved views_a[] = {{
        .data = (void*)values_a,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .shape = {2u},
        .element_strides = {1u},
    }};
    static const cxpr_view_resolved views_b[] = {{
        .data = (void*)values_b,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .shape = {2u},
        .element_strides = {1u},
    }};
    publication_fixture fixture = {
        .generation_a = {views_a, 1u, 101u},
        .generation_b = {views_b, 1u, 202u},
    };
    pthread_t readers[READER_COUNT];
    pthread_t publisher;
    atomic_init(&fixture.published, &fixture.generation_a);
    atomic_init(&fixture.ready, 0u);
    atomic_init(&fixture.start, false);
    atomic_init(&fixture.failures, 0u);
    atomic_init(&fixture.reads, 0u);
    for (size_t i = 0u; i < READER_COUNT; ++i)
        assert(pthread_create(&readers[i], NULL, reader_main, &fixture) == 0);
    assert(pthread_create(&publisher, NULL, publisher_main, &fixture) == 0);
    assert(pthread_join(publisher, NULL) == 0);
    for (size_t i = 0u; i < READER_COUNT; ++i)
        assert(pthread_join(readers[i], NULL) == 0);
    assert(atomic_load_explicit(&fixture.failures, memory_order_relaxed) == 0u);
    assert(atomic_load_explicit(&fixture.reads, memory_order_relaxed) ==
           (uint64_t)READER_COUNT * READER_ITERATIONS);
}

static void test_stale_containment_preserves_first_error(void) {
    double values[] = {4.0, 8.0};
    cxpr_view_resolved resolved = {
        .data = values,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .shape = {2u},
        .element_strides = {1u},
    };
    cxpr_view_table table = {&resolved, 1u, 7u};
    cxpr_view_execution_status status;
    cxpr_view_lease lease;
    cxpr_value value;
    double index = 9.0;
    cxpr_view_execution_status_reset(&status);
    assert(cxpr_view_lease_acquire(&table, 7u, &status, &lease) == CXPR_VIEW_OK);
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 41u, &value) ==
        CXPR_VIEW_INDEX_OUT_OF_RANGE);
    assert(value.type == CXPR_VALUE_NUMBER && value.d == 0.0);
    assert(status.code == CXPR_VIEW_INDEX_OUT_OF_RANGE);
    assert(status.view_slot == 0u && status.location == 41u);
    table.generation = 8u; /* Boundary replacement after the reader quiesces. */
    index = 0.0;
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 99u, &value) == CXPR_VIEW_STALE_BINDING);
    assert(value.d == 0.0);
    assert(status.code == CXPR_VIEW_INDEX_OUT_OF_RANGE);
    assert(status.location == 41u);
    assert(cxpr_view_lease_acquire(
        &table, 7u, &status, &lease) == CXPR_VIEW_STALE_BINDING);
}

int main(void) {
    test_concurrent_immutable_publication();
    test_stale_containment_preserves_first_error();
    puts("external view concurrency tests passed");
    return 0;
}
