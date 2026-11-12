#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cxpr/view.h>

static cxpr_view_requirement requirement(void) {
    cxpr_view_requirement value = {0};
    value.name = "samples";
    value.element_type = CXPR_VALUE_NUMBER;
    value.rank = 2u;
    value.max_shape[0] = 4u;
    value.max_shape[1] = 8u;
    value.min_alignment = _Alignof(double);
    value.access = CXPR_VIEW_ACCESS_READ_ONLY;
    return value;
}

static cxpr_view binding(double* data) {
    cxpr_view value = {0};
    value.data = data;
    value.element_type = CXPR_VALUE_NUMBER;
    value.rank = 2u;
    value.shape[0] = 3u;
    value.shape[1] = 5u;
    value.strides[0] = 5 * (ptrdiff_t)sizeof(double);
    value.strides[1] = (ptrdiff_t)sizeof(double);
    value.capacity_bytes = 15u * sizeof(double);
    value.flags = CXPR_VIEW_ACCESS_READ_ONLY | CXPR_VIEW_CONTIGUOUS;
    return value;
}

int main(void) {
    double data[15] = {0};
    cxpr_view_requirement required = requirement();
    cxpr_view bound = binding(data);
    cxpr_view_resolved resolved = {0};
    size_t failed = SIZE_MAX;

    assert(cxpr_views_resolve(
        &required, &bound, 1u, &resolved, &failed) == CXPR_VIEW_OK);
    assert(failed == SIZE_MAX);
    assert(resolved.data == data);
    assert(resolved.shape[0] == 3u && resolved.shape[1] == 5u);
    assert(resolved.element_strides[0] == 5u);
    assert(resolved.element_strides[1] == 1u);

    {
        const cxpr_view_table table = {&resolved, 1u, 7u};
        const double valid[] = {2.0, 4.0};
        const double outside[] = {9.0, 4.0};
        const double fractional[] = {1.5, 2.0};
        const double negative[] = {-3.0, 0.0};
        const double non_finite[] = {NAN, 0.0};
        cxpr_value value = cxpr_num(-1.0);
        size_t length = 0u;
        data[14] = 42.5;
        assert(cxpr_view_length(&table, 0u, 1u, &length) ==
               CXPR_VIEW_OK);
        assert(length == 5u);
        assert(cxpr_view_load_checked(
                   &table, 0u, valid, 2u, &value) == CXPR_VIEW_OK);
        assert(value.type == CXPR_VALUE_NUMBER && value.d == 42.5);
        assert(cxpr_view_load_checked(
                   &table, 0u, outside, 2u, &value) ==
               CXPR_VIEW_INDEX_OUT_OF_RANGE);
        assert(cxpr_view_load_or(
                   &table, 0u, outside, 2u, cxpr_num(12.0), &value) ==
               CXPR_VIEW_INDEX_OUT_OF_RANGE);
        assert(value.type == CXPR_VALUE_NUMBER && value.d == 12.0);
        assert(cxpr_view_load_clamped(
                   &table, 0u, outside, 2u, &value) == CXPR_VIEW_OK);
        assert(value.type == CXPR_VALUE_NUMBER && value.d == 42.5);
        assert(cxpr_view_load_clamped(
                   &table, 0u, fractional, 2u, &value) ==
               CXPR_VIEW_INVALID_INDEX);
        assert(cxpr_view_load_checked(
                   &table, 0u, non_finite, 2u, &value) ==
               CXPR_VIEW_INVALID_INDEX);
        assert(cxpr_view_load_or(
                   &table, 0u, outside, 2u, cxpr_int64(12), &value) ==
               CXPR_VIEW_TYPE_MISMATCH);
        assert(cxpr_view_load_checked(
                   &table, 1u, valid, 2u, &value) ==
               CXPR_VIEW_STALE_BINDING);
        data[0] = 3.25;
        assert(cxpr_view_load_clamped(
                   &table, 0u, negative, 2u, &value) == CXPR_VIEW_OK);
        assert(value.d == 3.25);
    }

    bound.shape[1] = 9u;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, &failed) ==
           CXPR_VIEW_SHAPE_EXCEEDED);
    assert(failed == 0u);

    bound = binding(data);
    bound.strides[0]++;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_NON_CONTIGUOUS);

    bound = binding(data);
    bound.capacity_bytes--;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_CAPACITY_EXCEEDED);

    bound = binding(data);
    bound.flags |= CXPR_VIEW_WRITABLE;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_INVALID_FLAGS);

    bound = binding(data);
    bound.element_type = CXPR_VALUE_INT64;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_TYPE_MISMATCH);

    bound = binding(data);
    bound.rank = 1u;
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_RANK_MISMATCH);

    bound = binding(NULL);
    assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
           CXPR_VIEW_MISSING_BINDING);

    {
        union {
            double alignment;
            unsigned char bytes[sizeof(data) + _Alignof(double)];
        } storage = {0};
        bound = binding((double*)(void*)(storage.bytes + 1u));
        assert(cxpr_views_resolve(&required, &bound, 1u, &resolved, NULL) ==
               CXPR_VIEW_MISALIGNED);
    }

    {
        cxpr_view_requirement huge = requirement();
        huge.max_shape[0] = SIZE_MAX;
        huge.max_shape[1] = 2u;
        bound = binding(data);
        bound.shape[0] = SIZE_MAX;
        bound.shape[1] = 2u;
        bound.strides[0] = 2 * (ptrdiff_t)sizeof(double);
        bound.strides[1] = (ptrdiff_t)sizeof(double);
        bound.capacity_bytes = SIZE_MAX;
        assert(cxpr_views_resolve(&huge, &bound, 1u, &resolved, NULL) ==
               CXPR_VIEW_OVERFLOW);
    }

    {
        int64_t integers[] = {INT64_C(7), INT64_C(9007199254740993)};
        bool booleans[] = {false, true};
        cxpr_view_resolved typed[2] = {0};
        const double second[] = {1.0};
        cxpr_value value = cxpr_num(0.0);
        typed[0].data = integers;
        typed[0].element_type = CXPR_VALUE_INT64;
        typed[0].rank = 1u;
        typed[0].shape[0] = 2u;
        typed[0].element_strides[0] = 1u;
        typed[1].data = booleans;
        typed[1].element_type = CXPR_VALUE_BOOL;
        typed[1].rank = 1u;
        typed[1].shape[0] = 2u;
        typed[1].element_strides[0] = 1u;
        const cxpr_view_table table = {typed, 2u, 1u};
        assert(cxpr_view_load_checked(
                   &table, 0u, second, 1u, &value) == CXPR_VIEW_OK);
        assert(value.type == CXPR_VALUE_INT64 &&
               value.i64 == INT64_C(9007199254740993));
        assert(cxpr_view_load_checked(
                   &table, 1u, second, 1u, &value) == CXPR_VIEW_OK);
        assert(value.type == CXPR_VALUE_BOOL && value.b);
    }

    puts("external view binding tests passed");
    return 0;
}
