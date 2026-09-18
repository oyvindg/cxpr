/*
 * R8 -- host-side bulk all-reduce for convergence: stub.
 *
 * Asserts that the proposed cxpr_bulk_reduce(view, column, op) matches a manual
 * reduction over the same output column, with a deterministic reduction order, so
 * the host can drive iterate-to-fixpoint without hand-rolled reductions.
 *
 * GATED behind CXPR_PATHFINDING_R8_READY (default off): cxpr_bulk_reduce does not
 * exist on release/3.2.0 yet. Passes as SKIP until R8 lands. R8 is P1/optional --
 * only pursue if hand-rolled convergence becomes a burden.
 *
 * See plans/field_pathfinding_requirements.md (R8).
 */
#include <stdio.h>

#ifndef CXPR_PATHFINDING_R8_READY
#define CXPR_PATHFINDING_R8_READY 0
#endif

#if CXPR_PATHFINDING_R8_READY

#include <cxpr/cxpr.h>
#include <cxpr/bulk.h>

#include <assert.h>
#include <math.h>

#define ELEMENTS 8

/* Manual reference reduction with a fixed (ascending index) order. */
static double manual_reduce_max(const cxpr_bulk_column* col, size_t count) {
    double acc = -INFINITY;
    for (size_t i = 0; i < count; ++i) {
        double v = col->values[i * col->stride].d;
        if (v > acc) acc = v;
    }
    return acc;
}

static double manual_reduce_sum(const cxpr_bulk_column* col, size_t count) {
    double acc = 0.0;
    for (size_t i = 0; i < count; ++i) acc += col->values[i * col->stride].d;
    return acc;
}

static void test_reduce_matches_manual(void) {
    cxpr_value residual[ELEMENTS];
    cxpr_bulk_column column = { residual, 1 };
    cxpr_bulk_view view = {0};
    double got, want;

    for (int i = 0; i < ELEMENTS; ++i) residual[i] = cxpr_num((double)(i % 3));
    view.outputs = &column;
    view.output_count = 1;
    view.element_count = ELEMENTS;

    /* op = max */
    want = manual_reduce_max(&column, ELEMENTS);
    assert(cxpr_bulk_reduce(&view, 0, CXPR_BULK_REDUCE_MAX, &got) == CXPR_BULK_OK);
    assert(got == want);

    /* op = sum, deterministic order */
    want = manual_reduce_sum(&column, ELEMENTS);
    assert(cxpr_bulk_reduce(&view, 0, CXPR_BULK_REDUCE_SUM, &got) == CXPR_BULK_OK);
    assert(got == want);
}

static void test_reduce_validates_bounds(void) {
    cxpr_bulk_view view = {0};
    double got;
    /* out-of-range column index must be rejected like the rest of the bulk API. */
    assert(cxpr_bulk_reduce(&view, 99, CXPR_BULK_REDUCE_SUM, &got)
           != CXPR_BULK_OK);
}

#endif /* CXPR_PATHFINDING_R8_READY */

int main(void) {
#if CXPR_PATHFINDING_R8_READY
    test_reduce_matches_manual();
    test_reduce_validates_bounds();
    printf("bulk_reduce: OK\n");
#else
    printf("bulk_reduce: SKIP "
           "(R8 cxpr_bulk_reduce not yet implemented on release/3.2.0)\n");
#endif
    return 0;
}
