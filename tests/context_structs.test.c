#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>

static void test_context_struct_storage_paths(void) {
    cxpr_context* ctx = cxpr_context_new();
    const char* fields[] = {"x", "y"};
    double values[] = {3.0, 4.0};
    const cxpr_struct_value* s;
    const cxpr_struct_value* cached;
    cxpr_struct_value* owned_cached;
    cxpr_value field;
    bool found = false;

    assert(ctx);
    cxpr_context_set_fields(ctx, "pose", fields, values, 2);
    s = cxpr_context_get_struct(ctx, "pose");
    assert(s);
    assert(s->field_count == 2);

    field = cxpr_context_get_field(ctx, "pose", "x", &found);
    assert(found);
    assert(field.type == CXPR_VALUE_NUMBER);
    assert(field.d == 3.0);

    cxpr_context_clear_cached_structs(ctx);
    field = cxpr_context_get_field(ctx, "pose", "y", &found);
    assert(found);
    assert(field.d == 4.0);

    {
        cxpr_value cached_values[] = {cxpr_fv_double(8.0), cxpr_fv_double(9.0)};
        owned_cached = cxpr_struct_value_new(fields, cached_values, 2);
        assert(owned_cached);
        cxpr_context_set_cached_struct(ctx, "cached_pose", owned_cached);
        cxpr_struct_value_free(owned_cached);
    }
    cached = cxpr_context_get_cached_struct(ctx, "cached_pose");
    assert(cached);
    assert(cached->field_count == 2);
    assert(cached->field_values[0].d == 8.0);
    cxpr_context_clear_cached_structs(ctx);
    assert(cxpr_context_get_cached_struct(ctx, "cached_pose") == NULL);

    cxpr_context_free(ctx);
}

int main(void) {
    test_context_struct_storage_paths();
    printf("  \xE2\x9C\x93 context_structs\n");
    return 0;
}
