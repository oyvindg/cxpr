#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <cxpr/view_lease.h>

int main(void) {
    double values[] = {4.0, 8.0};
    cxpr_view_resolved resolved = {
        .data = values,
        .element_type = CXPR_VALUE_NUMBER,
        .rank = 1u,
        .shape = {2u},
        .element_strides = {1u},
    };
    cxpr_view_table generation_a = {&resolved, 1u, 7u};
    cxpr_view_table generation_b = {&resolved, 1u, 8u};
    cxpr_view_execution_status status;
    cxpr_view_lease lease;
    cxpr_value value;
    double index = 1.0;

    cxpr_view_execution_status_reset(&status);
    assert(status.code == CXPR_VIEW_OK);
    assert(cxpr_view_lease_acquire(
        &generation_a, 7u, &status, &lease) == CXPR_VIEW_OK);
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 41u, &value) == CXPR_VIEW_OK);
    assert(value.type == CXPR_VALUE_NUMBER && value.d == 8.0);

    index = 3.0;
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 42u, &value) ==
        CXPR_VIEW_INDEX_OUT_OF_RANGE);
    assert(value.type == CXPR_VALUE_NUMBER && value.d == 0.0);
    assert(status.code == CXPR_VIEW_INDEX_OUT_OF_RANGE);
    assert(status.view_slot == 0u && status.location == 42u);

    index = -1.0;
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 99u, &value) ==
        CXPR_VIEW_INVALID_INDEX);
    assert(status.location == 42u); /* first error is stable */

    assert(cxpr_view_lease_acquire(
        &generation_b, 7u, &status, &lease) ==
        CXPR_VIEW_STALE_BINDING);
    assert(cxpr_view_lease_acquire(
        &generation_b, 8u, &status, &lease) == CXPR_VIEW_OK);
    generation_b.generation = 9u;
    index = 0.0;
    assert(cxpr_view_lease_load_contained(
        &lease, 0u, &index, 1u, 100u, &value) ==
        CXPR_VIEW_STALE_BINDING);
    assert(value.d == 0.0);

    puts("external view generation lease tests passed");
    return 0;
}
